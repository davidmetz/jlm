//
// Created by david on 7/2/21.
//

#include "jlm/backend/hls/rvsdg2rhls/mem-queue.hpp"
#include "jlm/ir/operators/store.hpp"
#include "jlm/ir/operators/load.hpp"
#include "jlm/ir/operators/lambda.hpp"
#include "jlm/backend/hls/rvsdg2rhls/mem-sep.hpp"
#include "jive/view.hpp"
#include <jive/rvsdg/substitution.hpp>
#include <jlm/ir/hls/hls.hpp>
#include <jive/rvsdg/theta.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <jlm/opt/alias-analyses/Operators.hpp>

void
jlm::hls::mem_queue(jlm::RvsdgModule &rm) {
    auto &graph = rm.Rvsdg();
    auto root = graph.root();
    mem_queue(root);
}

void
dump_xml(const jive::region * region, const std::string &file_name) {
    auto xml_file = fopen(file_name.c_str(), "w");
    jive::view_xml(region, xml_file);
    fclose(xml_file);
}

void find_load_store(
        jive::output * op,
        std::vector<jive::simple_node *> &load_nodes,
        std::vector<jive::simple_node *> &store_nodes,
//        std::vector<jive::simple_node *> &decouple_nodes,
        std::unordered_set<jive::output*> & visited){
    if(!dynamic_cast<const jlm::MemoryStateType*>(&op->type())){
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
//            }  else if (auto co = dynamic_cast<const jlm::CallOperation *>(&simplenode->operation())) {
//                // TODO: verify this is the right type of function call
//                decouple_nodes.push_back(simplenode);
            }
            for (size_t i = 0; i < simplenode->noutputs(); ++i) {
                find_load_store(simplenode->output(i), load_nodes, store_nodes, visited);
            }
        } else if (auto sti = dynamic_cast<jive::structural_input*>(user)){
            for(auto &arg: sti->arguments){
                find_load_store(&arg, load_nodes, store_nodes, visited);
            }
        } else if (auto r = dynamic_cast<jive::result*>(user)){
            if(auto ber = dynamic_cast<jlm::hls::backedge_result*>(r)){
                find_load_store(ber->argument(), load_nodes, store_nodes, visited);
            } else {
                find_load_store(r->output(), load_nodes, store_nodes, visited);
            }
        } else {
            JLM_UNREACHABLE("THIS SHOULD BE COVERED");
        }
    }
}

jive::structural_output * find_loop_output(jive::structural_input* sti){
    auto sti_arg = sti->arguments.first();
    assert(sti_arg->nusers() == 1);
    auto user = *sti_arg->begin();
    auto si = dynamic_cast<jive::simple_input*>(user);
    assert(dynamic_cast<const jlm::hls::mux_op*>(&si->node()->operation()));
    for (size_t i = 1; i < 3; ++i) {
        auto arg = si->node()->input(i)->origin();
        if(auto ba = dynamic_cast<jlm::hls::backedge_argument*>(arg)){
            auto res = ba->result();
            assert(res);
            auto buffer_out = dynamic_cast<jive::simple_output*>(res->origin());
            assert(buffer_out);
            assert(dynamic_cast<const jlm::hls::buffer_op*>(&buffer_out->node()->operation()));
            auto branch_out = dynamic_cast<jive::simple_output*>(buffer_out->node()->input(0)->origin());
            assert(branch_out);
            assert(dynamic_cast<const jlm::hls::branch_op*>(&branch_out->node()->operation()));
            // branch
            for (size_t j = 0; j < 2; ++j) {
                assert(branch_out->node()->output(j)->nusers() == 1);
                auto result = dynamic_cast<jive::result*>(*branch_out->node()->output(j)->begin());
                if(result){
                    return result->output();
                }
            }
        }
    }
    JLM_UNREACHABLE("This should never happen");
}

jive::output * route_to_region(jive::region *target, jive::output *out){
    // TODO: generalized routing for nested loops
    assert(out->region() == target);
    auto ln = dynamic_cast<jlm::hls::loop_node*>(target->node());
    assert(ln);
    auto arg = ln->add_backedge(out->type());
    arg->result()->divert_to(out);
    return arg;
}

jive::output * separate_load_edge(
        jive::output * common_edge,
        jive::output *load_edge,
        jive::simple_node * load,
        jive::output ** new_common_edge,
        std::vector<jive::output*> &store_addresses,
        std::vector<jive::output*> &store_dequeues
//        jive::substitution_map & smap
        ){
    // follows along common edge and routes load edge through the same regions
    // redirects the supplied load to the new edge and adds it to stores
    // the new edge might be routed through unnecessary regions. This should be fixed by running DNE
    while (true){
        // each iteration should update common_edge and/or new_edge
        assert(common_edge->nusers() == 1);
        if(load_edge->nusers() != 1){
            dump_xml(load_edge->region(), "no_users.rvsdg");
        }
        assert(load_edge->nusers() == 1);
        assert(common_edge != load_edge);
        auto user = *common_edge->begin();
        auto load_user = *load_edge->begin();
        if(auto res = dynamic_cast<jive::result*>(user)){
            JLM_UNREACHABLE("THIS SHOULD NOT HAPPEN");
            // end of region reached
//            return res;
        } else if (auto sti = dynamic_cast<jive::structural_input*>(user)){
            auto loop_node = dynamic_cast<jlm::hls::loop_node*>(sti->node());
            assert(loop_node);
            jive::output *buffer;

            load_edge = loop_node->add_loopvar(load_edge, &buffer);
            load_user->divert_to(load_edge);
            common_edge = find_loop_output(sti);
            auto sti_arg = sti->arguments.first();
            assert(sti_arg->nusers() == 1);
            auto user = *sti_arg->begin();
            auto si = dynamic_cast<jive::simple_input*>(user);
            assert(dynamic_cast<const jlm::hls::mux_op*>(&si->node()->operation()));
            assert(buffer->nusers()==1);
            separate_load_edge(si->node()->output(0), buffer, load, nullptr, store_addresses, store_dequeues);
        } else if (auto si = dynamic_cast<jive::simple_input*>(user)){
            auto sn = si->node();
            auto op = &si->node()->operation();

            if (auto br = dynamic_cast<const jlm::hls::branch_op *>(op)) {
                if(!br->loop){
                    // start of gamma
                    auto load_branch_out = jlm::hls::branch_op::create(*sn->input(0)->origin(), *load_edge, false);
                    for (size_t i = 0; i < sn->noutputs(); ++i) {
                        // dummy user for edge
                        jlm::hls::sink_op::create(*load_branch_out[i])[0];
                        auto dummy_user = dynamic_cast<jive::simple_input*>(*load_branch_out[i]->begin())->node();
                        // need both load and common edge here
                        load_branch_out[i] = separate_load_edge(sn->output(i), load_branch_out[i], load, &common_edge, store_addresses, store_dequeues);
                        assert(load_branch_out[i]->nusers() == 1);
                        assert(dummy_user->input(0)->origin() == load_branch_out[i]);
                        remove(dummy_user);
                    }
                    // create mux
                    assert(common_edge->nusers() == 1);
                    auto mux_user = dynamic_cast<jive::simple_input*>(*common_edge->begin());
                    assert(mux_user);
                    auto mux_op = dynamic_cast<const jlm::hls::mux_op*>(&mux_user->node()->operation());
                    assert(mux_op);
                    load_edge = jlm::hls::mux_op::create(*mux_user->node()->input(0)->origin(), load_branch_out, mux_op->discarding, false)[0];
                    load_user->divert_to(load_edge);
                    common_edge = mux_user->node()->output(0);
                } else {
                    // end of loop
                    auto load_user_input = dynamic_cast<jive::simple_input*>(load_user);
                    assert(load_user_input);
                    assert(dynamic_cast<const jlm::hls::branch_op *>(&load_user_input->node()->operation()));
                    return nullptr;
                }
            } else if (auto mx = dynamic_cast<const jlm::hls::mux_op *>(op)) {
                assert(!mx->loop);
                // end of gamma
                if(!new_common_edge){
                    dump_xml(load_edge->region(), "no_new_common_edge.rvsdg");
                }
                assert(new_common_edge);
                *new_common_edge = common_edge;
                return load_edge;
            } else if (dynamic_cast<const jlm::StoreOperation *>(op)) {
                // TODO: handle case where input to store is already a MemStateMerge
                auto sg_out = jlm::hls::state_gate_op::create(*sn->input(0)->origin(), {load_edge});
                auto load_split = jlm::MemStateSplitOperator::Create(sg_out[1], 2);
                load_edge = load_split[0];
                load_user->divert_to(load_edge);
                store_addresses.push_back(route_to_region(load->region(), sg_out[0]));
                std::vector<jive::output*> merge_operands({common_edge, load_split[1]});
                user->divert_to(jlm::MemStateMergeOperator::Create(merge_operands));
                common_edge = sn->output(0);
                assert(common_edge->nusers() == 1);
                user = *common_edge->begin();
                // TODO: handle case where input to store is already a MemStateSplit
                auto store_split = jlm::MemStateSplitOperator::Create(common_edge, 2);
                common_edge = store_split[0];
                user->divert_to(common_edge);
                store_dequeues.push_back(route_to_region(load->region(), store_split[1]));
            } else if (dynamic_cast<const jlm::LoadOperation *>(op)) {
                assert(sn->noutputs() == 2);
                if(sn == load){
                    dump_xml(load->region(), "before_divert1.rvsdg");
                    si->divert_to(load_edge);
                    sn->output(1)->divert_users(common_edge);
                    load_edge = sn->output(1);
                    dump_xml(load->region(), "before_divert2.rvsdg");
                    load_user->divert_to(load_edge);
                } else {
                    common_edge = sn->output(1);
                }
            } else if (auto co = dynamic_cast<const jlm::CallOperation *>(op)) {
                // TODO: verify this is the right type of function call
//                if(decouple_nodes.end() != std::find(decouple_nodes.begin(), decouple_nodes.end(),sn)){
//                    auto new_next = *new_edge->begin();
//                    si->divert_to(new_edge);
//                    assert(sn->noutputs() == 2);
//                    sn->output(1)->divert_users(common_edge);
//                    new_next->divert_to(sn->output(1));
//                }
                throw jlm::error("not implemented yet");

            } else if (auto co = dynamic_cast<const jlm::MemStateMergeOperator *>(op)) {
                auto si_load_user =dynamic_cast<jive::simple_input*>(load_user);
                if(si_load_user && si->node() == sn){
                    return nullptr;
                }
                //TODO: handle
                JLM_UNREACHABLE("THIS SHOULD NOT HAPPEN");
            } else {
                JLM_UNREACHABLE("THIS SHOULD NOT HAPPEN");
//                assert(sn->noutputs() == 1);
//                common_edge = sn->output(0);
            }
        } else {
            JLM_UNREACHABLE("THIS SHOULD NOT HAPPEN");
        }
    }
}

jive::output * process_loops(jive::output * state_edge){
    while (true){
        // each iteration should update state_edge
        assert(state_edge->nusers() == 1);
        auto user = *state_edge->begin();
        if(auto res = dynamic_cast<jive::result*>(user)){
            // end of region reached
            JLM_UNREACHABLE("This should never happen");
        } else if (auto si = dynamic_cast<jive::simple_input*>(user)){
            auto sn = si->node();
            auto op = &si->node()->operation();
            auto br = dynamic_cast<const jlm::hls::branch_op *>(op);
            if (br && !br->loop) {
                // start of gamma
                for (size_t i = 0; i < sn->noutputs(); ++i) {
                    state_edge = process_loops(sn->output(i));
                }
            } else if (dynamic_cast<const jlm::hls::mux_op *>(op)) {
                // end of gamma
                assert(sn->noutputs() == 1);
                return sn->output(0);
            } else if (dynamic_cast<const jlm::aa::LambdaExitMemStateOperator *>(op)) {
                // end of lambda
                assert(sn->noutputs() == 1);
                return sn->output(0);
            } else {
                assert(sn->noutputs() == 1);
                state_edge = sn->output(0);
            }
        } else if (auto sti = dynamic_cast<jive::structural_input*>(user)){
            auto ln = dynamic_cast<jlm::hls::loop_node*>(sti->node());
            assert(ln);
            // update to output of loop
            auto common_edge_after_loop = find_loop_output(sti);
            assert(common_edge_after_loop->nusers() == 1);
            auto common_user = *common_edge_after_loop->begin();

            std::vector<jive::simple_node *> load_nodes;
            std::vector<jive::simple_node *> store_nodes;
            std::unordered_set<jive::output*> visited;
            // this is a hack to keep search within the loop
            visited.insert(common_edge_after_loop);
            find_load_store(&*sti->arguments.begin(), load_nodes, store_nodes, visited);
            if(load_nodes.size()+store_nodes.size()<2){
                state_edge = common_edge_after_loop;
                continue;
            }
            auto split_states = jlm::MemStateSplitOperator::Create(sti->origin(), load_nodes.size()+1);
            // handle common edge
            auto common_edge = split_states[0];
            sti->divert_to(common_edge);
            split_states[0] = common_edge_after_loop;
            state_edge = jlm::MemStateMergeOperator::Create(split_states);
            common_user->divert_to(state_edge);
            for (size_t i = 0; i < load_nodes.size(); ++i) {
                auto load = load_nodes[i];
                auto load_edge = split_states[1+i];
                std::vector<jive::output*> store_addresses;
                std::vector<jive::output*> store_dequeues;
//                jive::substitution_map smap;
//                smap.insert(common_edge, load_edge);
                separate_load_edge(common_edge, load_edge, load, nullptr, store_addresses, store_dequeues);
                assert(store_nodes.size() == store_addresses.size());
                assert(store_nodes.size() == store_dequeues.size());
                for (size_t j = 0; j < store_nodes.size(); ++j) {
                    load->input(0)->divert_to(jlm::hls::addr_queue_op::create(*load->input(0)->origin(), *store_addresses[i], *store_dequeues[i]));
                }
            }
        } else {
            JLM_UNREACHABLE("THIS SHOULD NOT HAPPEN");
        }
    }
}

void jlm::hls::mem_queue(jive::region *region) {
    auto lambda = dynamic_cast<const jlm::lambda::node *>(region->nodes.first());
    auto lambda_region = lambda->subregion();
    auto state_arg = GetMemoryStateArgument(*lambda);
    assert(state_arg->nusers()==1);
    auto state_user = *state_arg->begin();
    auto entry_input = dynamic_cast<jive::simple_input*>(state_user);
    assert(entry_input);
    auto entry_node = entry_input->node();
    assert(dynamic_cast<const jlm::aa::LambdaEntryMemStateOperator*>(&entry_node->operation()));
    // for each state edge:
    //    for each outer loop (theta/loop in lambda region):
    //        split state edge before the loop
    //         * one edge for only stores (preserves store order)
    //         * a separate edge for each load, going through the stores as well
    //        merge state edges after the loop
    //        for each load:
    //            insert store address queue before address input of load
    //             * enq order of stores guaranteed by load edge, deq by store edge
    //            for each store:
    //                insert state gate addr enq + deq after store complete

    for (size_t i = 0; i < entry_node->noutputs(); ++i) {
        jive::output * state_edge = entry_node->output(i);
        process_loops(state_edge);
    }
}


