//
// Created by david on 7/2/21.
//

#include "jlm/backend/hls/rvsdg2rhls/mem-conv.hpp"
#include "jlm/ir/operators/store.hpp"
#include "jlm/ir/operators/load.hpp"
#include "jlm/ir/operators/lambda.hpp"
#include <jive/rvsdg/substitution.hpp>
#include <jlm/ir/hls/hls.h>
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
        auto result = jive::result::create(request->region(), request, output, request->type());
        return route_request(target, output);
    }
}

jive::simple_node *replace_load(jive::simple_node *orig, jive::output *resp) {
    auto addr = orig->input(0)->origin();
    std::vector<jive::output *> states;
    for (int i = 1; i < orig->ninputs(); ++i) {
        states.push_back(orig->input(i)->origin());
    }
    auto outputs = jlm::hls::load_op::create(*addr, states, *resp);
    auto nn = dynamic_cast<jive::node_output*>(outputs[0])->node();
    for (int i = 0; i < orig->noutputs(); ++i) {
        orig->output(i)->divert_users(nn->output(i));
    }
    remove(orig);
    return dynamic_cast<jive::simple_node*>(nn);
}

jive::simple_node *replace_store(jive::simple_node *orig) {
    auto addr = orig->input(0)->origin();
    auto data = orig->input(1)->origin();
    std::vector<jive::output *> states;
    for (int i = 2; i < orig->ninputs(); ++i) {
        states.push_back(orig->input(i)->origin());
    }
    auto outputs = jlm::hls::store_op::create(*addr, *data, states);
    auto nn = dynamic_cast<jive::node_output*>(outputs[0])->node();
    for (int i = 0; i < orig->noutputs(); ++i) {
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

void jlm::hls::mem_conv(jive::region *region) {
    auto lambda = dynamic_cast<const jlm::lambda::node *>(region->nodes.begin().ptr());
    auto lambda_region = lambda->subregion();
    std::vector<jive::simple_node *> load_nodes;
    std::vector<jive::simple_node *> store_nodes;
    gather_mem_nodes(region, load_nodes, store_nodes);
    std::vector<const jive::type *> load_types;
    for (auto l: load_nodes) {
        auto pointerType = dynamic_cast<const PointerType*>(&l->input(0)->type());
        load_types.push_back(&pointerType->GetElementType());
    }
    auto load_responses = mem_resp_op::create(lambda_region, load_types);
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
}


