//
// Created by david on 7/2/21.
//

#include "jlm/backend/hls/rvsdg2rhls/mem-conv.hpp"
#include "jlm/ir/operators/store.hpp"
#include "jlm/ir/operators/load.hpp"
#include "jlm/ir/operators/lambda.hpp"
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

jive::output *route_response(jive::region *target, jive::output *response) {
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

jive::output *route_request(jive::region *target, jive::output *request) {
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

void gather_mem_nodes(jive::region *region, std::vector<jive::simple_node *> &load_nodes,
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

typedef std::vector<std::pair<std::vector<jive::simple_node *>, std::vector<jive::simple_node *>>> port_load_store;

// trace each input pointer to loads and stores
void trace_pointer_arguments(jive::output * op, std::vector<jive::simple_node *> &load_nodes,
                             std::vector<jive::simple_node *> &store_nodes,
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
            } else {
                for (size_t i = 0; i < simplenode->noutputs(); ++i) {
                    trace_pointer_arguments(simplenode->output(i), load_nodes, store_nodes, visited);
                }
            }
        } else if (auto sti = dynamic_cast<jive::structural_input*>(user)){
            for(auto &arg: sti->arguments){
                trace_pointer_arguments(&arg, load_nodes, store_nodes, visited);
            }
        } else if (auto r = dynamic_cast<jive::result*>(user)){
            if(auto ber = dynamic_cast<jlm::hls::backedge_result*>(r)){
                trace_pointer_arguments(ber->argument(), load_nodes, store_nodes, visited);
            } else {
                trace_pointer_arguments(r->output(), load_nodes, store_nodes, visited);
            }
        } else {
            JLM_UNREACHABLE("THIS SHOULD BE COVERED");
        }
    }
}

void trace_pointer_arguments(const jlm::lambda::node * ln, port_load_store &port_nodes){
    for (size_t i = 0; i < ln->subregion()->narguments(); ++i) {
        auto arg = ln->subregion()->argument(i);
        if(dynamic_cast<const jlm::PointerType*>(&arg->type())){
            port_nodes.emplace_back();
            std::unordered_set<jive::output*> visited;
            trace_pointer_arguments(arg, port_nodes.back().first, port_nodes.back().second, visited);
        }
    }
}

void jlm::hls::mem_conv(jive::region *region) {
    auto lambda = dynamic_cast<const jlm::lambda::node *>(region->nodes.begin().ptr());
    auto lambda_region = lambda->subregion();
    std::vector<jive::simple_node *> load_nodes;
    std::vector<jive::simple_node *> store_nodes;
    gather_mem_nodes(region, load_nodes, store_nodes);
    port_load_store port_nodes;
    trace_pointer_arguments(lambda, port_nodes);
    //TODO: extra port for loads/stores not associated to a port yet
    for(auto & ls: port_nodes){
        generate_mem_port(lambda_region, ls.first, ls.second);
    }
}

void jlm::hls::generate_mem_port(jive::region *lambda_region, std::vector<jive::simple_node *> &load_nodes,
                                 const std::vector<jive::simple_node *> &store_nodes) {
    std::vector<const jive::type *> load_types;
    for (auto l: load_nodes) {
        auto pointerType = dynamic_cast<const PointerType*>(&l->input(0)->type());
        load_types.push_back(&pointerType->GetElementType());
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


