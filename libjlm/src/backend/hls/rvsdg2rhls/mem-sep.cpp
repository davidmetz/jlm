//
// Created by david on 7/2/21.
//

#include "jlm/backend/hls/rvsdg2rhls/mem-sep.hpp"
#include "jlm/ir/operators/store.hpp"
#include "jlm/ir/operators/load.hpp"
#include "jlm/ir/operators/lambda.hpp"
#include "jlm/backend/hls/rvsdg2rhls/mem-conv.hpp"
#include <jive/rvsdg/substitution.hpp>
#include <jlm/ir/hls/hls.hpp>
#include <jive/rvsdg/theta.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <jlm/opt/alias-analyses/Operators.hpp>
#include <jive/view.hpp>

void
jlm::hls::mem_sep_independent(jlm::RvsdgModule &rm) {
    auto &graph = rm.Rvsdg();
    auto root = graph.root();
    mem_sep_independent(root);
}

void
jlm::hls::mem_sep_argument(jlm::RvsdgModule &rm) {
    auto &graph = rm.Rvsdg();
    auto root = graph.root();
    mem_sep_argument(root);
}

// from MemoryStateEncoder.cpp
jive::argument *
jlm::hls::GetMemoryStateArgument(const jlm::lambda::node & lambda)
{
    auto subregion = lambda.subregion();
    for (size_t n = 0; n < subregion->narguments(); n++) {
        auto argument = subregion->argument(n);
        if (jive::is<jlm::MemoryStateType>(argument->type()))
            return argument;
    }

    JLM_UNREACHABLE("This should have never happened!");
}

jive::result *
jlm::hls::GetMemoryStateResult(const jlm::lambda::node & lambda)
{
    auto subregion = lambda.subregion();
    for (size_t n = 0; n < subregion->nresults(); n++) {
        auto result = subregion->result(n);
        if (jive::is<jlm::MemoryStateType>(result->type()))
            return result;
    }

    JLM_UNREACHABLE("This should have never happened!");
}

void gather_mem_nodes(jive::region *region, std::vector<jive::simple_node *> &mem_nodes) {
    for (auto &node: jive::topdown_traverser(region)) {
        if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
            for (size_t n = 0; n < structnode->nsubregions(); n++)
                gather_mem_nodes(structnode->subregion(n), mem_nodes);
        } else if (auto simplenode = dynamic_cast<jive::simple_node * >(node)) {
            if (dynamic_cast<const jlm::StoreOperation *>(&simplenode->operation())) {
                mem_nodes.push_back(simplenode);
            } else if (dynamic_cast<const jlm::LoadOperation *>(&simplenode->operation())) {
                mem_nodes.push_back(simplenode);
            }
        }
    }
}

jive::output *route_through(jive::region *target, jive::output *response) {
    if (response->region() == target) {
        return response;
    } else {
        auto parent_response = route_through(target->node()->region(), response);
        auto parrent_user = *parent_response->begin();
        if (auto gn = dynamic_cast<jive::gamma_node*>(target->node())){
            auto ip = gn->add_entryvar(parent_response);
            std::vector<jive::output*> vec;
            for(auto &arg:ip->arguments){
                vec.push_back(&arg);
            }
            parrent_user->divert_to(gn->add_exitvar(vec));
            for (auto &arg:ip->arguments) {
                if(arg.region() == target){
                    return &arg;
                }
            }
            JLM_UNREACHABLE("THIS SHOULD NOT HAPPEN");
        } else if (auto tn = dynamic_cast<jive::theta_node*>(target->node())){
            auto lv = tn->add_loopvar(parent_response);
            parrent_user->divert_to(lv);
            return lv->argument();
        }
        JLM_UNREACHABLE("THIS SHOULD NOT HAPPEN");
    }
}
/* assign each load and store its own state edge. */
void jlm::hls::mem_sep_independent(jive::region *region) {
    auto lambda = dynamic_cast<const jlm::lambda::node *>(region->nodes.begin().ptr());
    auto lambda_region = lambda->subregion();
    auto state_arg = GetMemoryStateArgument(*lambda);
    auto state_user = *state_arg->begin();
    std::vector<jive::simple_node *> mem_nodes;
    gather_mem_nodes(lambda_region, mem_nodes);
    auto entry_states = jlm::aa::LambdaEntryMemStateOperator::Create(state_arg, 1+mem_nodes.size());
    auto entry_node = dynamic_cast<const jive::node_output*>(entry_states[0])->node();
    auto state_result = GetMemoryStateResult(*lambda);
    // handle existing state edge - TODO: remove entirely?
    state_user->divert_to(entry_states.back());
    entry_states.pop_back();
    entry_states.push_back(state_result->origin());
    auto merged_state = jlm::aa::LambdaExitMemStateOperator::Create(lambda_region, entry_states);
    entry_states.pop_back();
    state_result->divert_to(merged_state);
    for (auto node : mem_nodes) {
        auto in_state = route_through(node->region(), entry_states.back());
        auto out_state = *in_state->begin();
        auto node_input = node->input(node->ninputs()-1);
        auto old_in_state = node_input->origin();
        node_input->divert_to(in_state);
        auto node_output = node->output(node->noutputs()-1);
        JLM_ASSERT(node_output->nusers()==1);
        node->output(node->noutputs()-1)->divert_users(old_in_state);
        out_state->divert_to(node_output);
        entry_states.pop_back();
    }
}

jive::result *trace_edge(
        jive::output *common_edge,
        jive::output *new_edge,
        std::vector<jive::simple_node *> &load_nodes,
        const std::vector<jive::simple_node *> &store_nodes,
        std::vector<jive::simple_node *> &decouple_nodes) {
    // follows along common edge and routes new edge through the same regions
    // redirects the supplied loads, stores and decouples to the new edge
    // the new edge might be routed through unnecessary regions. This should be fixed by running DNE
    while (true){
        // each iteration should update common_edge and/or new_edge
        assert(common_edge->nusers() == 1);
        assert(new_edge->nusers() == 1);
        auto user = *common_edge->begin();
        auto new_next = *new_edge->begin();
        if(auto res = dynamic_cast<jive::result*>(user)){
            // end of region reached
            return res;
        } else if (auto gi = dynamic_cast<jive::gamma_input*>(user)){
            auto gn = gi->node();
            auto ip = gn->add_entryvar(new_edge);
            std::vector<jive::output*> vec;
            for(auto &arg:ip->arguments){
                vec.push_back(&arg);
            }
            new_edge = gn->add_exitvar(vec);
            new_next->divert_to(new_edge);
            for (size_t i = 0; i < gn->nsubregions(); ++i) {
                auto subres = trace_edge(gi->argument(i), ip->argument(i), load_nodes, store_nodes, decouple_nodes);
                common_edge = subres->output();
            }
        } else if (auto ti = dynamic_cast<jive::theta_input*>(user)){
            auto tn = ti->node();
            auto lv = tn->add_loopvar(new_edge);
            trace_edge(ti->argument(), lv->argument(), load_nodes, store_nodes, decouple_nodes);
            common_edge = ti->output();
            new_edge = lv;
            new_next->divert_to(new_edge);
        } else if (auto si = dynamic_cast<jive::simple_input*>(user)){
            auto sn = si->node();
            auto op = &si->node()->operation();
            if (dynamic_cast<const jlm::StoreOperation *>(op)) {
                assert(sn->noutputs() == 1);
                if(store_nodes.end() != std::find(store_nodes.begin(), store_nodes.end(),sn)){
                    si->divert_to(new_edge);
                    sn->output(0)->divert_users(common_edge);
                    new_edge = sn->output(0);
                    new_next->divert_to(new_edge);
                } else {
                    common_edge = sn->output(0);
                }
            } else if (dynamic_cast<const jlm::LoadOperation *>(op)) {
                assert(sn->noutputs() == 2);
                if(load_nodes.end() != std::find(load_nodes.begin(), load_nodes.end(),sn)){
                    auto new_next = *new_edge->begin();
                    si->divert_to(new_edge);
                    sn->output(1)->divert_users(common_edge);
                    new_next->divert_to(sn->output(1));
                    new_edge = sn->output(1);
                    new_next->divert_to(new_edge);
                } else {
                    common_edge = sn->output(1);
                }
            }  else if (auto co = dynamic_cast<const jlm::CallOperation *>(op)) {
                // TODO: verify this is the right type of function call
//                if(decouple_nodes.end() != std::find(decouple_nodes.begin(), decouple_nodes.end(),sn)){
//                    auto new_next = *new_edge->begin();
//                    si->divert_to(new_edge);
//                    assert(sn->noutputs() == 2);
//                    sn->output(1)->divert_users(common_edge);
//                    new_next->divert_to(sn->output(1));
//                }
                throw jlm::error("not implemented yet");

            } else {
                assert(sn->noutputs() == 1);
                common_edge = sn->output(0);
            }
        } else {
            JLM_UNREACHABLE("THIS SHOULD NOT HAPPEN");
        }
    }
}

/* assign each pointer argument its own state edge. */
void jlm::hls::mem_sep_argument(jive::region *region) {
    auto lambda = dynamic_cast<const jlm::lambda::node *>(region->nodes.begin().ptr());
    auto lambda_region = lambda->subregion();
    auto state_arg = GetMemoryStateArgument(*lambda);
    auto state_user = *state_arg->begin();
    port_load_store_decouple port_nodes;
    trace_pointer_arguments(lambda, port_nodes);
    auto entry_states = jlm::aa::LambdaEntryMemStateOperator::Create(state_arg, 1+port_nodes.size());
    auto entry_node = dynamic_cast<const jive::node_output*>(entry_states[0])->node();
    auto state_result = GetMemoryStateResult(*lambda);
    // handle existing state edge - TODO: remove entirely?
    auto common_edge = entry_states.back();
    entry_states.pop_back();
    state_user->divert_to(common_edge);
    entry_states.push_back(state_result->origin());
    auto merged_state = jlm::aa::LambdaExitMemStateOperator::Create(lambda_region, entry_states);
    entry_states.pop_back();
    state_result->divert_to(merged_state);
    for (auto tp : port_nodes) {
        auto new_edge = entry_states.back();
        entry_states.pop_back();
        trace_edge(common_edge, new_edge, std::get<0>(tp), std::get<1>(tp), std::get<2>(tp));
    }
}


