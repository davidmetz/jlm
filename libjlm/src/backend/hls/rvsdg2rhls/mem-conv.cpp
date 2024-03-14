/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * and Magnus Sjalander <work@sjalander.com>
 * See COPYING for terms of redistribution.
 */

#include "jlm/backend/hls/rvsdg2rhls/mem-conv.hpp"
#include "jlm/backend/hls/rvsdg2rhls/rvsdg2rhls.hpp"
#include "jlm/ir/operators/store.hpp"
#include "jlm/ir/operators/load.hpp"
#include "jlm/ir/operators/lambda.hpp"
#include "jlm/ir/operators/call.hpp"
#include "jive/types/bitstring/constant.hpp"
#include "jive/view.hpp"
#include "jlm/backend/hls/rvsdg2rhls/rhls-dne.hpp"
#include "jlm/backend/hls/rvsdg2rhls/remove-unused-state.hpp"
#include <jive/rvsdg/substitution.hpp>
#include <jlm/ir/hls/hls.hpp>
#include <jive/rvsdg/theta.hpp>
#include <jive/rvsdg/traverser.hpp>

jive::output *jlm::hls::route_response(jive::region *target, jive::output *response) {
    if (response->region() == target) {
        return response;
    } else {
        auto parent_response = route_response(target->node()->region(), response);
        auto ln = dynamic_cast<jlm::hls::loop_node*>(target->node());
        JLM_ASSERT(ln);
        auto input = jive::structural_input::create(ln, parent_response, parent_response->port());
        auto argument = jive::argument::create(target, input, response->type());
        return argument;
    }
}

jive::output *jlm::hls::route_request(jive::region *target, jive::output *request) {
    if (request->region() == target) {
        return request;
    } else {
        auto ln = dynamic_cast<jlm::hls::loop_node*>(request->region()->node());
        JLM_ASSERT(ln);
        auto output = jive::structural_output::create(ln, request->port());
        jive::result::create(request->region(), request, output, request->type());
        return route_request(target, output);
    }
}

jive::simple_node *replace_load(jive::simple_node *orig, jive::output *resp) {
    auto addr = orig->input(0)->origin();
    std::vector<jive::output *> states;
    for (size_t i = 1; i < orig->ninputs(); ++i) {
        states.push_back(orig->input(i)->origin());
    }
    jive::node* nn;
    if(states.empty()){
        auto outputs = jlm::hls::decoupled_load_op::create(*addr, *resp);
        nn = dynamic_cast<jive::node_output*>(outputs[0])->node();
    } else{
        auto outputs = jlm::hls::load_op::create(*addr, states, *resp);
        nn = dynamic_cast<jive::node_output*>(outputs[0])->node();
    }
    for (size_t i = 0; i < orig->noutputs(); ++i) {
        orig->output(i)->divert_users(nn->output(i));
    }
    remove(orig);
    return dynamic_cast<jive::simple_node*>(nn);
}

const jive::bitconstant_op * trace_channel(const jive::output * dst){
    if (auto arg = dynamic_cast<const jive::argument*>(dst)){
        return trace_channel(arg->input()->origin());
    } else if (auto so = dynamic_cast<const jive::simple_output*>(dst)){
        if(auto co = dynamic_cast<const jive::bitconstant_op*>(&so->node()->operation())){
            return co;
        }
        for (size_t i = 0; i < so->node()->ninputs(); ++i) {
            //TODO: fix, this is a hack
            if(so->node()->input(i)->type() == dst->type()){
                return trace_channel(so->node()->input(i)->origin());
            }
        }
    }
    JLM_UNREACHABLE("Channel not found");
}

const jive::output *trace_call(const jive::output *output);

const jive::output *trace_call(const jive::input *input) {
    // version of trace call for rhls
    return trace_call(input->origin());
}

const jive::output *trace_call(const jive::output *output) {
    // version of trace call for rhls
    if(auto argument = dynamic_cast<const jive::argument *>(output)){
        auto graph = output->region()->graph();
        if (argument->region() == graph->root()){
            return argument;
        } else if(auto ba = dynamic_cast<const jlm::hls::backedge_argument*>(argument)){
            // don't follow backedges to avoid cycles
            return nullptr;
        }
        return trace_call(argument->input());
    } else if(auto so = dynamic_cast<const jive::structural_output *>(output)){
        for (auto &r: so->results) {
            if(auto result = trace_call(&r)){
                return result;
            }
        }
    }  else if(auto so = dynamic_cast<const jive::simple_output *>(output)){
        for (size_t i = 0; i < so->node()->ninputs(); ++i) {
            auto ip = so->node()->input(i);
            if(ip->type() == output->type()){
                if(auto result = trace_call(ip)){
                    return result;
                }
            }
        }
    } else {
        JLM_UNREACHABLE("");
    }
    return nullptr;
}

std::string get_impport_function_name(jive::input *input){
    auto traced = trace_call(input);
    JLM_ASSERT(traced);
    auto arg = dynamic_cast<const jive::argument *>(traced);
    JLM_ASSERT(arg);
    auto ip = dynamic_cast<const jlm::impport *>(&arg->port());
    JLM_ASSERT(ip);
    return ip->name();
}

// trace function ptr to its call
void trace_function_calls(jive::output * op, std::vector<jive::simple_node*> &calls,
                             std::unordered_set<jive::output*> & visited) {
/*
    if(auto pt = dynamic_cast<const jlm::PointerType*>(&op->type())){
        // only process fucntion pointer outputs
        if(!dynamic_cast<const jlm::FunctionType *>(&pt->GetElementType())){
            return;
        }
    } else{
        return;
    }
*/
    if(visited.count(op)){
        // skip already processed outputs
        return;
    }
    visited.insert(op);
    for (auto user: *op) {
        if(auto si = dynamic_cast<jive::simple_input*>(user)){
            auto simplenode = si->node();
            if (auto co = dynamic_cast<const jlm::CallOperation *>(&simplenode->operation())) {
                // TODO: verify this is the right type of function call
                calls.push_back(simplenode);
            } else {
                for (size_t i = 0; i < simplenode->noutputs(); ++i) {
                    trace_function_calls(simplenode->output(i), calls, visited);
                }
            }
        } else if (auto sti = dynamic_cast<jive::structural_input*>(user)){
            for(auto &arg: sti->arguments){
                trace_function_calls(&arg, calls, visited);
            }
        } else if (auto r = dynamic_cast<jive::result*>(user)){
            if(auto ber = dynamic_cast<jlm::hls::backedge_result*>(r)){
                trace_function_calls(ber->argument(), calls, visited);
            } else {
                trace_function_calls(r->output(), calls, visited);
            }
        } else {
            JLM_UNREACHABLE("THIS SHOULD BE COVERED");
        }
    }
}

jive::simple_node *find_decouple_response(const jlm::lambda::node * lambda, const jive::bitconstant_op *request_constant) {
    jive::argument * response_function;
    for (size_t i = 0; i < lambda->ncvarguments(); ++i) {
        auto ip = lambda->cvargument(i)->input();
        if(dynamic_cast<const jlm::PointerType *>(ip) && get_impport_function_name(ip) == "decouple_response"){
            response_function = lambda->cvargument(i);
        }
    }
    JLM_ASSERT(response_function);
    std::vector<jive::simple_node*> reponse_calls;
    std::unordered_set<jive::output*> visited;
    trace_function_calls(response_function, reponse_calls, visited);
    JLM_ASSERT(!reponse_calls.empty());
    for (auto & rc:reponse_calls) {
        auto response_constant = trace_channel(rc->input(1)->origin());
        if(*response_constant == *request_constant){
            return rc;
        }
    }
    JLM_UNREACHABLE("No response found");
}

jive::simple_node *replace_decouple(const jlm::lambda::node * lambda, jive::simple_node *decouple_request, jive::output *resp) {
    JLM_ASSERT(dynamic_cast<const jlm::CallOperation *>(&decouple_request->operation()));
    auto channel = decouple_request->input(1)->origin();
    auto channel_constant = trace_channel(channel);

    auto decouple_response = find_decouple_response(lambda, channel_constant);


    auto addr = decouple_request->input(2)->origin();
    // connect out states to in states
    // TODO: use stategate?
    // one function pointer + 2 params
    for (size_t i = 3; i < decouple_request->ninputs(); ++i) {
        decouple_request->output(i-3)->divert_users(decouple_request->input(i)->origin());
    }
    for (size_t i = 2; i < decouple_response->ninputs(); ++i) {
        decouple_response->output(i-2+1)->divert_users(decouple_response->input(i)->origin());
    }
    // create this outside loop - need to tunnel outward from request and inward to response
    auto routed_addr = jlm::hls::route_request(lambda->subregion(), addr);
    // response is not routed inward for this case
    auto dload_out = jlm::hls::decoupled_load_op::create(*routed_addr, *resp);
    // use a buffer here to make ready logic for response easy and consistent
    auto buf = jlm::hls::buffer_op::create(*dload_out[0], 2, true)[0];

    auto routed_data = jlm::hls::route_response(decouple_response->region(), buf);
    decouple_response->output(0)->divert_users(routed_data);
    // TODO: handle state edges?
    remove(decouple_request);
    remove(decouple_response);

    auto nn = dynamic_cast<jive::node_output*>(dload_out[0])->node();
    return dynamic_cast<jive::simple_node*>(nn);
}

jive::simple_node *replace_store(jive::simple_node *orig) {
    auto addr = orig->input(0)->origin();
    auto data = orig->input(1)->origin();
    std::vector<jive::output *> states;
    for (size_t i = 2; i < orig->ninputs(); ++i) {
        states.push_back(orig->input(i)->origin());
    }
    auto outputs = jlm::hls::store_op::create(*addr, *data, states);
    auto nn = dynamic_cast<jive::node_output*>(outputs[0])->node();
    for (size_t i = 0; i < orig->noutputs(); ++i) {
        orig->output(i)->divert_users(nn->output(i));
    }
    remove(orig);
    return dynamic_cast<jive::simple_node*>(nn);
}

void gather_mem_nodes(
        jive::region *region,
        std::vector<jive::simple_node *> &load_nodes,
        std::vector<jive::simple_node *> &store_nodes,
        std::vector<jive::simple_node *> &decouple_nodes,
        std::unordered_set<jive::simple_node *> exclude) {
    for (auto &node: jive::topdown_traverser(region)) {
        if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
            for (size_t n = 0; n < structnode->nsubregions(); n++)
                gather_mem_nodes(structnode->subregion(n), load_nodes, store_nodes, decouple_nodes, exclude);
        } else if (auto simplenode = dynamic_cast<jive::simple_node * >(node)) {
            if(exclude.find(simplenode) != exclude.end()){
                continue;
            } if (dynamic_cast<const jlm::StoreOperation *>(&simplenode->operation())) {
                store_nodes.push_back(simplenode);
            } else if (dynamic_cast<const jlm::LoadOperation *>(&simplenode->operation())) {
                load_nodes.push_back(simplenode);
            } else if (auto co = dynamic_cast<const jlm::CallOperation *>(&simplenode->operation())) {
                // TODO: verify this is the right type of function call
                decouple_nodes.push_back(simplenode);
            }
        }
    }
}

// trace each input pointer to loads and stores
void trace_pointer_argument(jive::output * op, std::vector<jive::simple_node *> &load_nodes,
                       std::vector<jive::simple_node *> &store_nodes,
                       std::vector<jive::simple_node *> &decouple_nodes,
                       std::unordered_set<jive::output*> & visited) {
    if(!dynamic_cast<const jlm::PointerType*>(&op->type())){
        // only process pointer outputs
        return;
    }
    if(visited.count(op)){
        // skip already processed outputs
        return;
    }
    visited.insert(op);
    for (auto user: *op) {
        if(auto si = dynamic_cast<jive::simple_input*>(user)){
            auto simplenode = si->node();
            if (dynamic_cast<const jlm::StoreOperation *>(&simplenode->operation())) {
                store_nodes.push_back(simplenode);
            } else if (dynamic_cast<const jlm::LoadOperation *>(&simplenode->operation())) {
                load_nodes.push_back(simplenode);
            }  else if (auto co = dynamic_cast<const jlm::CallOperation *>(&simplenode->operation())) {
                // TODO: verify this is the right type of function call
                decouple_nodes.push_back(simplenode);
            } else {
                for (size_t i = 0; i < simplenode->noutputs(); ++i) {
                    trace_pointer_argument(simplenode->output(i), load_nodes, store_nodes, decouple_nodes, visited);
                }
            }
        } else if (auto sti = dynamic_cast<jive::structural_input*>(user)){
            for(auto &arg: sti->arguments){
                trace_pointer_argument(&arg, load_nodes, store_nodes, decouple_nodes, visited);
            }
        } else if (auto r = dynamic_cast<jive::result*>(user)){
            if(auto ber = dynamic_cast<jlm::hls::backedge_result*>(r)){
                trace_pointer_argument(ber->argument(), load_nodes, store_nodes, decouple_nodes, visited);
            } else {
                trace_pointer_argument(r->output(), load_nodes, store_nodes, decouple_nodes, visited);
            }
        } else {
            JLM_UNREACHABLE("THIS SHOULD BE COVERED");
        }
    }
}

void jlm::hls::trace_pointer_arguments(const jlm::lambda::node * ln, port_load_store_decouple &port_nodes){
    for (size_t i = 0; i < ln->subregion()->narguments(); ++i) {
        auto arg = ln->subregion()->argument(i);
        if(auto pt = dynamic_cast<const jlm::PointerType*>(&arg->type())){
            if(trace_call(arg->input()) != nullptr){
                // skip function pointers
                continue;
            }
            port_nodes.emplace_back();
            std::unordered_set<jive::output*> visited;
            trace_pointer_argument(arg, std::get<0>(port_nodes.back()), std::get<1>(port_nodes.back()), std::get<2>(port_nodes.back()), visited);
        }
    }
}

void
jlm::hls::MemoryConverter(jlm::RvsdgModule &rm)
{
    //
    // Replacing memory nodes with nodes that have explicit memory ports requires arguments and results to be added to the lambda. The arguments must be added before the memory nodes are replaced, else the input of the new memory node will be left dangling, which is not allowed. We therefore need to first replace the lambda node with a new lambda node that has the new arguments and results. We can then replace the memory nodes and connect them to the new arguments.
    //

    auto root = rm.Rvsdg().root();
    auto lambda = dynamic_cast<jlm::lambda::node *>(root->nodes.begin().ptr());

    //
    // Converting loads and stores to explicitly use memory ports
    // This modifies the function signature so we create a new lambda node to replace the old one
    //
    auto oldFunctionType = lambda->type();
	std::vector<const jive::type *> newArgumentTypes;
	for (size_t i = 0; i < oldFunctionType.NumArguments(); ++i) {
        newArgumentTypes.push_back(&oldFunctionType.ArgumentType(i));
	}
	std::vector<const jive::type *> newResultTypes;
	for (size_t i = 0; i < oldFunctionType.NumResults(); ++i) {
        newResultTypes.push_back(&oldFunctionType.ResultType(i));
	}

    //
    // Get the load and store nodes and add an argument and result for each to represent the memory response and request ports
    //
    port_load_store_decouple portNodes;
    trace_pointer_arguments(lambda, portNodes);

    auto responseTypePtr = get_mem_res_type(jive::bit64);
    auto requestTypePtr = get_mem_req_type(jive::bit64, false);
    auto requestTypePtrWrite = get_mem_req_type(jive::bit64, true);

    std::unordered_set<jive::simple_node *> accountedNodes;
    for (auto & portNode : portNodes)
    {
        newArgumentTypes.push_back(responseTypePtr.get());
        if (std::get<1>(portNode).empty())
        {
            newResultTypes.push_back(requestTypePtr.get());
        }
        else
        {
            newResultTypes.push_back(requestTypePtrWrite.get());
        }
        accountedNodes.insert(std::get<0>(portNode).begin(), std::get<0>(portNode).end());
        accountedNodes.insert(std::get<1>(portNode).begin(), std::get<1>(portNode).end());
        accountedNodes.insert(std::get<2>(portNode).begin(), std::get<2>(portNode).end());
    }
    std::vector<jive::simple_node *> unknownLoadNodes;
    std::vector<jive::simple_node *> unknownStoreNodes;
    std::vector<jive::simple_node *> unknownDecoupledNodes;
    gather_mem_nodes(root, unknownLoadNodes, unknownStoreNodes, unknownDecoupledNodes, accountedNodes);
    if(!unknownLoadNodes.empty() || !unknownStoreNodes.empty() || !unknownDecoupledNodes.empty())
    {
        // Extra port for loads/stores not associated to a port yet (i.e., unknown base pointer)
        newArgumentTypes.push_back(responseTypePtr.get());
        if (unknownStoreNodes.empty())
        {
            newResultTypes.push_back(requestTypePtr.get());
        }
        else
        {
            newResultTypes.push_back(requestTypePtrWrite.get());
        }
    }

    //
    // Create new lambda and copy the region from the old lambda
    //
	FunctionType newFunctionType(newArgumentTypes, newResultTypes);
	auto newLambda = jlm::lambda::node::create(lambda->region(), newFunctionType, lambda->name(), lambda->linkage(), lambda->attributes());

	jive::substitution_map smap;
	for (size_t i = 0; i < lambda->ncvarguments(); ++i)
    {
		smap.insert(lambda->cvargument(i), newLambda->add_ctxvar(lambda->cvargument(i)->input()->origin()));
	}

	for (size_t i = 0; i < lambda->nfctarguments(); ++i)
    {
        smap.insert(lambda->fctargument(i), newLambda->fctargument(i));
	}
	lambda->subregion()->copy(newLambda->subregion(), smap, false, false);

    //
    // All memory nodes need to be replaced with new nodes that have explicit memory ports.
    // This needs to happen first and the smap needs to be updated with the new nodes,
    // before we can use the original lambda results and look them up in the updated smap.
    //

    std::vector<jive::output *> newResults;
    // The new arguments are placed directly after the original arguments so we create an index that points to the first new argument
    auto newArgumentsIndex = lambda->nfctarguments();
    for (auto & portNode : portNodes)
    {
        auto loadNodes = std::get<0>(portNode);
        auto storeNodes = std::get<1>(portNode);
        auto decoupledNodes = std::get<2>(portNode);
        newResults.push_back(ConnectRequestResponseMemPorts(newLambda, newArgumentsIndex++, smap, loadNodes, storeNodes, decoupledNodes));
    }
    if(!unknownLoadNodes.empty() || !unknownStoreNodes.empty() || !unknownDecoupledNodes.empty())
    {
        newResults.push_back(ConnectRequestResponseMemPorts(newLambda, newArgumentsIndex++, smap, unknownLoadNodes, unknownStoreNodes, unknownDecoupledNodes));
    }

	std::vector<jive::output *> originalResults;
    for (auto & result : lambda->fctresults())
    {
        originalResults.push_back(smap.lookup(result.origin()));
	}
    originalResults.insert(originalResults.end(), newResults.begin(), newResults.end());
	auto newOut = newLambda->finalize(originalResults);
    jive::result::create(newLambda->region(), newOut, nullptr, newOut->type());

	JLM_ASSERT(lambda->output()->nusers()==1);
	lambda->region()->remove_result((*lambda->output()->begin())->index());
    remove(lambda);

    // Remove imports for decouple_ function pointers
    dne(newLambda->subregion());

    auto xmlFile = fopen("before_remove_unused_state.rvsdg", "w");
    jive::view_xml(root, xmlFile);
    fclose(xmlFile);

    //
    // TODO
    // Remove unused state also creates a new lambda, which we have already done above.
    // It would be better to apply this functionality above such that we only create a new lambda once.
    //
    remove_unused_state(root);
    // Go through in reverse since we are removing things
    for (int i = newLambda->ncvarguments()-1; i >= 0; --i)
    {
        auto cvarg = newLambda->cvargument(i);
        if(auto pointerType = dynamic_cast<const jlm::PointerType*>(&cvarg->type()))
        {
            if (trace_call(cvarg->input()) != nullptr)
            {
                JLM_ASSERT(cvarg->nusers() == 0);
                auto cvip = cvarg->input();
                auto imp = cvip->origin();
                newLambda->subregion()->remove_argument(cvarg->index());
                //TODO: work around const
                newLambda->remove_input(cvip->index());
                auto imparg = dynamic_cast<jive::argument*>(imp);
                JLM_ASSERT(imparg);
                auto impprt = dynamic_cast<const impport *>(&imparg->port());
                JLM_ASSERT(impprt);
                root->remove_argument(imparg->index());
            }
        }
    }
}

jive::output *
jlm::hls::ConnectRequestResponseMemPorts(
    const jlm::lambda::node * lambda,
    size_t argumentIndex,
    jive::substitution_map & smap,
    const std::vector<jive::simple_node *> & originalLoadNodes,
    const std::vector<jive::simple_node *> & originalStoreNodes,
    const std::vector<jive::simple_node *> & originalDecoupledNodes)
{
    //
    // We have the memory operations from the original lambda and need to lookup the corresponding nodes in the new lambda
    //
    std::vector<jive::simple_node *> loadNodes;
    for (auto loadNode : originalLoadNodes)
    {
        JLM_ASSERT(smap.contains(*loadNode->output(0)));
        auto loadOutput = dynamic_cast<jive::simple_output *>(smap.lookup(loadNode->output(0)));
        loadNodes.push_back(loadOutput->node());
    }
    std::vector<jive::simple_node *> storeNodes;
    for (auto storeNode : originalStoreNodes)
    {
        JLM_ASSERT(smap.contains(*storeNode->output(0)));
        auto storeOutput = dynamic_cast<jive::simple_output *>(smap.lookup(storeNode->output(0)));
        storeNodes.push_back(storeOutput->node());
    }
    std::vector<jive::simple_node *> decoupledNodes;
    for (auto decoupledNode : originalDecoupledNodes)
    {
        JLM_ASSERT(smap.contains(*decoupledNode->output(0)));
        auto decoupledOutput = dynamic_cast<jive::simple_output *>(smap.lookup(decoupledNode->output(0)));
        decoupledNodes.push_back(decoupledOutput->node());
    }

    auto lambdaRegion = lambda->subregion();

    std::vector<const jive::type *> loadTypes;
    for (auto load: loadNodes)
    {
        JLM_ASSERT(dynamic_cast<const PointerType*>(&load->input(0)->type()));
        loadTypes.push_back(&load->output(0)->type());
    }
    for (auto decoupled: decoupledNodes)
    {
        JLM_ASSERT(dynamic_cast<const jlm::CallOperation *>(&decoupled->operation()));
        //TODO: precisions
        loadTypes.push_back(&jive::bit32);
    }
    auto loadResponses = mem_resp_op::create(*lambdaRegion->argument(argumentIndex), loadTypes);
    std::vector<jive::output *> loadAddresses;
    for (size_t i = 0; i < loadNodes.size(); ++i)
    {
        auto routed = route_response(loadNodes[i]->region(), loadResponses[i]);
        // The smap contains the nodes from the original lambda so we need to use the original load node when replacing the load since the smap must be updated
        auto replacement = ReplaceLoad(smap, originalLoadNodes[i], routed);
        auto addr = route_request(lambdaRegion, replacement->output(replacement->noutputs()-1));
        loadAddresses.push_back(addr);
    }
    for (size_t i = 0; i < decoupledNodes.size(); ++i)
    {
        JLM_UNREACHABLE("Handling of decoupled loads has not been updated after changing to the version were a new lambda is created.");

        auto reponse = loadResponses[+loadNodes.size()+i];
        auto node = decoupledNodes[i];

        auto replacement = replace_decouple(lambda, node, reponse);
        // TODO: routing is probably not necessary
        auto addr = route_request(lambdaRegion, replacement->output(1));
        loadAddresses.push_back(addr);
    }
    std::vector<jive::output *> storeOperands;
    for (size_t i = 0; i < storeNodes.size(); ++i)
    {
        // The smap contains the nodes from the original lambda so we need to use the oringal store node when replacing the store since the smap must be updated
        auto replacement = ReplaceStore(smap, originalStoreNodes[i]);
        auto addr = route_request(lambdaRegion, replacement->output(replacement->noutputs()-2));
        auto data = route_request(lambdaRegion, replacement->output(replacement->noutputs()-1));
        storeOperands.push_back(addr);
        storeOperands.push_back(data);
    }

    std::vector<const jive::valuetype *> loadValueTypes;
    for (auto type : loadTypes)
    {
        loadValueTypes.push_back(dynamic_cast<const jive::valuetype *>(type));
    }
    return mem_req_op::create(loadAddresses, storeOperands, loadValueTypes, lambdaRegion)[0];
}

jive::simple_node *
jlm::hls::ReplaceLoad(
    jive::substitution_map & smap,
    const jive::simple_node * originalLoad,
    jive::output * response)
{
    // We have the load from the original lambda since it is needed to update the smap
    // We need the load in the new lambda such that we can replace it with a load node with explicit memory ports
    auto replacedLoad = ((jive::simple_output *)smap.lookup(originalLoad->output(0)))->node();

    auto loadAddress = replacedLoad->input(0)->origin();
    std::vector<jive::output *> states;
    for (size_t i = 1; i < replacedLoad->ninputs(); ++i)
    {
        states.push_back(replacedLoad->input(i)->origin());
    }

    jive::node * newLoad;
    if(states.empty())
    {
        auto outputs = jlm::hls::decoupled_load_op::create(*loadAddress, *response);
        newLoad = dynamic_cast<jive::node_output*>(outputs[0])->node();
    }
    else
    {
        auto outputs = jlm::hls::load_op::create(*loadAddress, states, *response);
        newLoad = dynamic_cast<jive::node_output*>(outputs[0])->node();
    }

    for (size_t i = 0; i < replacedLoad->noutputs(); ++i)
    {
        smap.insert(originalLoad->output(i), newLoad->output(i));
        replacedLoad->output(i)->divert_users(newLoad->output(i));
    }
    remove(replacedLoad);
    return dynamic_cast<jive::simple_node*>(newLoad);
}

jive::simple_node *
jlm::hls::ReplaceStore(
    jive::substitution_map & smap,
    const jive::simple_node * originalStore)
{
    // We have the store from the original lambda since it is needed to update the smap
    // We need the store in the new lambda such that we can replace it with a store node with explicit memory ports
    auto replacedStore = ((jive::simple_output *)smap.lookup(originalStore->output(0)))->node();

    auto addr = replacedStore->input(0)->origin();
    auto data = replacedStore->input(1)->origin();
    std::vector<jive::output *> states;
    for (size_t i = 2; i < replacedStore->ninputs(); ++i)
    {
        states.push_back(replacedStore->input(i)->origin());
    }
    auto outputs = jlm::hls::store_op::create(*addr, *data, states);
    auto newStore = dynamic_cast<jive::node_output*>(outputs[0])->node();
    for (size_t i = 0; i < replacedStore->noutputs(); ++i)
    {
        smap.insert(originalStore->output(i), newStore->output(i));
        replacedStore->output(i)->divert_users(newStore->output(i));
    }
    remove(replacedStore);
    return dynamic_cast<jive::simple_node*>(newStore);
}

jive::simple_node *
ReplaceDecouple(
    jive::substitution_map & smap,
    const jlm::lambda::node * lambda,
    jive::simple_node *originalDecoupleRequest,
    jive::output *response)
{
    // We have the load from the original lambda since it is needed to update the smap
    // We need the store in the new lambda such that we can replace it with a store node with explicit memory ports
    auto decoupleRequest = ((jive::simple_output *)smap.lookup(originalDecoupleRequest->output(0)))->node();

    JLM_ASSERT(dynamic_cast<const jlm::CallOperation *>(&decoupleRequest->operation()));
    auto channel = decoupleRequest->input(1)->origin();
    auto channelConstant = trace_channel(channel);

    auto decoupledResponse = find_decouple_response(lambda, channelConstant);

    auto addr = decoupleRequest->input(2)->origin();
    // connect out states to in states
    // TODO: use stategate?
    // one function pointer + 2 params
    for (size_t i = 3; i < decoupleRequest->ninputs(); ++i) {
        decoupleRequest->output(i-3)->divert_users(decoupleRequest->input(i)->origin());
    }
    for (size_t i = 2; i < decoupledResponse->ninputs(); ++i) {
        decoupledResponse->output(i-2+1)->divert_users(decoupledResponse->input(i)->origin());
    }
    // create this outside loop - need to tunnel outward from request and inward to response
    auto routedAddress = jlm::hls::route_request(lambda->subregion(), addr);
    // response is not routed inward for this case
    auto decoupledLoadOutput = jlm::hls::decoupled_load_op::create(*routedAddress, *response);
    // use a buffer here to make ready logic for response easy and consistent
    auto buf = jlm::hls::buffer_op::create(*decoupledLoadOutput[0], 2, true)[0];

    auto routedData = jlm::hls::route_response(decoupledResponse->region(), buf);
    decoupledResponse->output(0)->divert_users(routedData);
    // TODO: handle state edges?
    remove(decoupleRequest);
    remove(decoupledResponse);

    auto nodeOutput = dynamic_cast<jive::node_output*>(decoupledLoadOutput[0])->node();
    return dynamic_cast<jive::simple_node*>(nodeOutput);
}