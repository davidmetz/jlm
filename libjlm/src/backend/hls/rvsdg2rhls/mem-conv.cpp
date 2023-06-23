//
// Created by david on 7/2/21.
//

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

void
jlm::hls::mem_conv(jlm::RvsdgModule &rm) {
    auto &graph = rm.Rvsdg();
    auto root = graph.root();
    mem_conv(root);
}

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
    auto outputs = jlm::hls::load_op::create(*addr, states, *resp);
    auto nn = dynamic_cast<jive::node_output*>(outputs[0])->node();
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

inline bool
is_fct_ptr(const jive::input *input) {
    auto pt = dynamic_cast<const jlm::PointerType *>(&input->type());
    if (!pt)
        return false;
    return dynamic_cast<const jlm::FunctionType *>(&pt->GetElementType());
}

const jive::output *trace_call(const jive::output *output);

const jive::output *trace_call(const jive::input *input) {
    // version of trace call for rhls
    JLM_ASSERT(is_fct_ptr(input));
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

//jive::simple_node * find_decouple_response(const jive::output * op, std::unordered_set<const jive::output*> visited);
//
//jive::simple_node * find_decouple_response(const jive::input * ip, std::unordered_set<const jive::output*> visited){
//    if(auto si = dynamic_cast<const jive::simple_input*>(ip)){
//        if(auto call_op = dynamic_cast<const jlm::CallOperation *>(&si->node()->operation())){
//            auto function_name = get_impport_function_name(si->node()->input(0));
//            if(function_name == "decouple_response"){
//                return si->node();
//            }
//        } else {
//            // TODO: not sure about this section...
//            for (size_t i = 0; i < si->node()->noutputs(); ++i) {
//                auto op = si->node()->output(i);
//                if(op->type() == ip->type()){
//                    auto result = find_decouple_response(op, visited);
//                    if(result!= nullptr){
//                        return result;
//                    }
//                }
//            }
//        }
//    } else if(auto si = dynamic_cast<const jive::structural_input*>(ip)) {
//        for (auto &a: si->arguments) {
//            auto result = find_decouple_response(&a, visited);
//            if(result!= nullptr){
//                return result;
//            }
//        }
//    }  else if(auto r = dynamic_cast<const jive::result*>(ip)) {
//        if(auto br = dynamic_cast<const jlm::hls::backedge_result*>(ip)){
//            auto result = find_decouple_response(br->argument(), visited);
//            if(result!= nullptr){
//                return result;
//            }
//        } else{
//            auto result = find_decouple_response(r->output(), visited);
//            if(result!= nullptr){
//                return result;
//            }
//        }
//    } else {
//        JLM_UNREACHABLE("");
//    }
//    return nullptr;
//}
//
//jive::simple_node *find_decouple_response(const jive::output *op, std::unordered_set<const jive::output*> visited) {
//    if(visited.count(op)){
//        return nullptr;
//    }
//    visited.insert(op);
//    //TODO:
//    for (auto user: *op) {
//        auto result = find_decouple_response(user, visited);
//        if(result!= nullptr){
//            return result;
//        }
//    }
//    return nullptr;
//}


// trace function ptr to its call
void trace_function_calls(jive::output * op, std::vector<jive::simple_node*> &calls,
                             std::unordered_set<jive::output*> & visited) {
    if(auto pt = dynamic_cast<const jlm::PointerType*>(&op->type())){
        // only process fucntion pointer outputs
        if(!dynamic_cast<const jlm::FunctionType *>(&pt->GetElementType())){
            return;
        }
    } else{
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
        if(is_fct_ptr(ip) && get_impport_function_name(ip) == "decouple_response"){
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
        std::vector<jive::simple_node *> &store_nodes) {
    for (auto &node: jive::topdown_traverser(region)) {
        if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
            for (size_t n = 0; n < structnode->nsubregions(); n++)
                gather_mem_nodes(structnode->subregion(n), load_nodes, store_nodes);
        } else if (auto simplenode = dynamic_cast<jive::simple_node * >(node)) {
            if (dynamic_cast<const jlm::StoreOperation *>(&simplenode->operation())) {
                store_nodes.push_back(simplenode);
            } else if (dynamic_cast<const jlm::LoadOperation *>(&simplenode->operation())) {
                load_nodes.push_back(simplenode);
            }
        }
    }
}

typedef std::vector<std::tuple<std::vector<jive::simple_node *>, std::vector<jive::simple_node *>, std::vector<jive::simple_node *>>> port_load_store_decouple;

// trace each input pointer to loads and stores
void trace_pointer_arguments(jive::output * op, std::vector<jive::simple_node *> &load_nodes,
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
                    trace_pointer_arguments(simplenode->output(i), load_nodes, store_nodes, decouple_nodes, visited);
                }
            }
        } else if (auto sti = dynamic_cast<jive::structural_input*>(user)){
            for(auto &arg: sti->arguments){
                trace_pointer_arguments(&arg, load_nodes, store_nodes, decouple_nodes, visited);
            }
        } else if (auto r = dynamic_cast<jive::result*>(user)){
            if(auto ber = dynamic_cast<jlm::hls::backedge_result*>(r)){
                trace_pointer_arguments(ber->argument(), load_nodes, store_nodes, decouple_nodes, visited);
            } else {
                trace_pointer_arguments(r->output(), load_nodes, store_nodes, decouple_nodes, visited);
            }
        } else {
            JLM_UNREACHABLE("THIS SHOULD BE COVERED");
        }
    }
}

void trace_pointer_arguments(const jlm::lambda::node * ln, port_load_store_decouple &port_nodes){
    for (size_t i = 0; i < ln->subregion()->narguments(); ++i) {
        auto arg = ln->subregion()->argument(i);
        if(auto pt = dynamic_cast<const jlm::PointerType*>(&arg->type())){
            if(dynamic_cast<const jlm::FunctionType *>(&pt->GetElementType())){
                // skip function pointers
                continue;
            }
            port_nodes.emplace_back();
            std::unordered_set<jive::output*> visited;
            trace_pointer_arguments(arg, std::get<0>(port_nodes.back()), std::get<1>(port_nodes.back()), std::get<2>(port_nodes.back()), visited);
        }
    }
}

void jlm::hls::mem_conv(jive::region *root) {
    auto lambda = dynamic_cast<jlm::lambda::node *>(root->nodes.begin().ptr());
    std::vector<jive::simple_node *> load_nodes;
    std::vector<jive::simple_node *> store_nodes;
    gather_mem_nodes(root, load_nodes, store_nodes);
    port_load_store_decouple port_nodes;
    trace_pointer_arguments(lambda, port_nodes);
    //TODO: extra port for loads/stores not associated to a port yet (i.e. unknown base pointer)
    for(auto & ls: port_nodes){
        generate_mem_port(lambda, std::get<0>(ls), std::get<1>(ls), std::get<2>(ls));
    }
    // remove impports for decouple_ function pointers
    dne(lambda->subregion());

    auto xml_file = fopen("before_remove_unused_state.rvsdg", "w");
    jive::view_xml(root, xml_file);
    fclose(xml_file);

    remove_unused_state(root);
    // lambda might have been changed
    lambda = dynamic_cast<jlm::lambda::node *>(root->nodes.begin().ptr());
    // go through in reverse since we are removing things
    for (int i = lambda->ncvarguments()-1; i >= 0; --i) {
        auto cvarg = lambda->cvargument(i);
        if(auto pt = dynamic_cast<const jlm::PointerType*>(&cvarg->type())) {
            if (dynamic_cast<const jlm::FunctionType *>(&pt->GetElementType())) {
                JLM_ASSERT(cvarg->nusers() == 0);
                auto cvip = cvarg->input();
                auto imp = cvip->origin();
                lambda->subregion()->remove_argument(cvarg->index());
                //TODO: work around const
                lambda->remove_input(cvip->index());
                auto imparg = dynamic_cast<jive::argument*>(imp);
                JLM_ASSERT(imparg);
                auto impprt = dynamic_cast<const impport *>(&imparg->port());
                JLM_ASSERT(impprt);
                root->remove_argument(imparg->index());
            }
        }
    }
}

void jlm::hls::generate_mem_port(const jlm::lambda::node * lambda,
                                 std::vector<jive::simple_node *> &load_nodes,
                                 const std::vector<jive::simple_node *> &store_nodes,
                                 std::vector<jive::simple_node *> &decouple_nodes) {
    auto lambda_region = lambda->subregion();
    std::vector<const jive::type *> load_types;
    for (auto l: load_nodes) {
        auto pointerType = dynamic_cast<const PointerType*>(&l->input(0)->type());
        load_types.push_back(&pointerType->GetElementType());
    }
    for (auto d: decouple_nodes) {
        JLM_ASSERT(dynamic_cast<const jlm::CallOperation *>(&d->operation()));
        //TODO: precisions
        load_types.push_back(&jive::bit32);
    }
    // TODO: change type
    auto mem_resp_type = get_mem_res_type(jive::bit64);
    auto mem_resp_argument = jive::argument::create(lambda_region, nullptr, *mem_resp_type);
    auto load_responses = mem_resp_op::create(*mem_resp_argument, load_types);
    std::vector<jive::output *> load_addrs;
    for (size_t i = 0; i < load_nodes.size(); ++i) {
        auto reponse = load_responses[i];
        auto node = load_nodes[i];
        auto routed = route_response(node->region(), reponse);
        auto replacement = replace_load(node, routed);
        auto addr = route_request(lambda_region, replacement->output(replacement->noutputs()-1));
        load_addrs.push_back(addr);
    }
    for (size_t i = 0; i < decouple_nodes.size(); ++i) {
        auto reponse = load_responses[+load_nodes.size()+i];
        auto node = decouple_nodes[i];

        auto xml_file = fopen("before_replace_decouple.rvsdg", "w");
        jive::view_xml(lambda_region->graph()->root(), xml_file);
        fclose(xml_file);

        auto replacement = replace_decouple(lambda, node, reponse);
        // TODO: routing is probably not necessary
        auto addr = route_request(lambda_region, replacement->output(1));
        load_addrs.push_back(addr);
    }
    std::vector<jive::output *> store_operands;
    for (size_t i = 0; i < store_nodes.size(); ++i) {
        auto node = store_nodes[i];
        auto replacement = replace_store(node);
        auto addr = route_request(lambda_region, replacement->output(replacement->noutputs()-2));
        auto data = route_request(lambda_region, replacement->output(replacement->noutputs()-1));
        store_operands.push_back(addr);
        store_operands.push_back(data);
    }
    auto req = mem_req_op::create(load_addrs, store_operands, lambda_region);
    jive::result::create(lambda_region, req[0], nullptr, req[0]->type());
}


