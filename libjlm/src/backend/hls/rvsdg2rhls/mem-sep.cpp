//
// Created by david on 7/2/21.
//

#include "jlm/backend/hls/rvsdg2rhls/mem-sep.hpp"
#include "jlm/ir/operators/store.hpp"
#include "jlm/ir/operators/load.hpp"
#include "jlm/ir/operators/lambda.hpp"
#include <jive/rvsdg/substitution.hpp>
#include <jlm/ir/hls/hls.hpp>
#include <jive/rvsdg/theta.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <jlm/opt/alias-analyses/Operators.hpp>

void
jlm::hls::mem_sep(jlm::RvsdgModule &rm) {
    auto &graph = rm.Rvsdg();
    auto root = graph.root();
    mem_sep(root);
}

// from MemoryStateEncoder.cpp
static jive::argument *
GetMemoryStateArgument(const jlm::lambda::node & lambda)
{
    auto subregion = lambda.subregion();
    for (size_t n = 0; n < subregion->narguments(); n++) {
        auto argument = subregion->argument(n);
        if (jive::is<jlm::MemoryStateType>(argument->type()))
            return argument;
    }

    JLM_UNREACHABLE("This should have never happened!");
}

static jive::result *
GetMemoryStateResult(const jlm::lambda::node & lambda)
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
void jlm::hls::mem_sep(jive::region *region) {
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


