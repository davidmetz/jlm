/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#include "jlm/backend/hls/rvsdg2rhls/remove-redundant-buf.hpp"
#include "jlm/opt/alias-analyses/Operators.hpp"
#include "jlm/ir/hls/hls.hpp"
#include <jive/rvsdg/traverser.hpp>
#include <jive/view.hpp>

bool breaks_cycle(jive::output * o, jive::structural_input ** loop_input = nullptr){
    if(auto so = dynamic_cast<jive::simple_output*>(o)){
        auto node = so->node();
        if (auto branch = dynamic_cast<const jlm::hls::branch_op *>(&node->operation())){
            return breaks_cycle(node->input(1)->origin(), loop_input);
        } else if (auto fork = dynamic_cast<const jlm::hls::fork_op *>(&node->operation())){
            // part of memory disambiguation
            return breaks_cycle(node->input(0)->origin(), loop_input);
        } else if (auto local_load = dynamic_cast<const jlm::hls::local_load_op *>(&node->operation())){
            return true;
        } else if (auto local_store = dynamic_cast<const jlm::hls::local_store_op *>(&node->operation())){
            return true;
        } else if (auto load = dynamic_cast<const jlm::hls::load_op *>(&node->operation())){
            return true;
        } else if (auto dl = dynamic_cast<const jlm::hls::decoupled_load_op *>(&node->operation())){
            return true;
        } else if (auto mr = dynamic_cast<const jlm::hls::mem_resp_op *>(&node->operation())){
            return true;
        }  else if (auto mri = dynamic_cast<const jlm::hls::mem_resp_id_op *>(&node->operation())){
            return true;
        } else if (auto store = dynamic_cast<const jlm::hls::store_op *>(&node->operation())){
            return true;
        } else if (auto buffer = dynamic_cast<const jlm::hls::buffer_op *>(&node->operation())){
            if(buffer->pass_through){
                return breaks_cycle(node->input(0)->origin(), loop_input);
            }
            return true;
        } else if (auto mux = dynamic_cast<const jlm::hls::mux_op *>(&node->operation())){
            if(mux->loop){
                auto arg = dynamic_cast<jive::argument*>(node->input(1)->origin());
                JLM_ASSERT(arg);
                if(loop_input) {
                    JLM_ASSERT(*loop_input == nullptr);
                    *loop_input = arg->input();
                }
                return false;
            }
            bool cycle_breaker = true;
            for (size_t i = 1; i < node->ninputs(); ++i) {
                cycle_breaker &= breaks_cycle(node->input(i)->origin(), loop_input);
            }
            return cycle_breaker;
        } else if (auto sg = dynamic_cast<const jlm::hls::state_gate_op *>(&node->operation())){
            bool cycle_breaker = true;
            for (size_t i = 0; i < node->ninputs(); ++i) {
                cycle_breaker &= breaks_cycle(node->input(i)->origin(), loop_input);
            }
            return cycle_breaker;
        }
    } else if(auto be = dynamic_cast<jlm::hls::backedge_argument*>(o)){
        return breaks_cycle(be->result()->origin(), loop_input);
    } else if(auto sto = dynamic_cast<jive::structural_output*>(o)){
        jive::structural_input * sti = nullptr;
        if(breaks_cycle(sto->results.begin()->origin(), &sti)){
            return true;
        } else if(sti){
            return breaks_cycle(sti->origin(), loop_input);
        } else {
            JLM_UNREACHABLE("could not find loop input");
        }  
    }
    JLM_UNREACHABLE("unexpected");
}

bool should_not_be_buffered(jive::output * o, jive::structural_input ** loop_input= nullptr){
    // contains operations that prevent a loop from getting an II of 1 if a buffer is placed after them
    if(auto so = dynamic_cast<jive::simple_output*>(o)){
        auto node = so->node();
        if (auto branch = dynamic_cast<const jlm::hls::branch_op *>(&node->operation())){
            return should_not_be_buffered(node->input(1)->origin(), loop_input);
        } else if (auto fork = dynamic_cast<const jlm::hls::fork_op *>(&node->operation())){
            // part of memory disambiguation
            return should_not_be_buffered(node->input(0)->origin(), loop_input);
        } else if (auto local_load = dynamic_cast<const jlm::hls::local_load_op *>(&node->operation())){
            return true;
        } else if (auto local_store = dynamic_cast<const jlm::hls::local_store_op *>(&node->operation())){
            return true;
        } else if (auto load = dynamic_cast<const jlm::hls::load_op *>(&node->operation())){
            return true;
        } else if (auto dl = dynamic_cast<const jlm::hls::decoupled_load_op *>(&node->operation())){
            return true;
        } else if (auto mr = dynamic_cast<const jlm::hls::mem_resp_op *>(&node->operation())){
            return true;
        }  else if (auto mri = dynamic_cast<const jlm::hls::mem_resp_id_op *>(&node->operation())){
            return true;
        } else if (auto store = dynamic_cast<const jlm::hls::store_op *>(&node->operation())){
            return true;
        } else if (auto buffer = dynamic_cast<const jlm::hls::buffer_op *>(&node->operation())){
            if(buffer->pass_through){
                return should_not_be_buffered(node->input(0)->origin(), loop_input);
            }
            return true;
        } else if (auto mux = dynamic_cast<const jlm::hls::mux_op *>(&node->operation())){
            if(mux->loop){
                auto arg = dynamic_cast<jive::argument*>(node->input(1)->origin());
                JLM_ASSERT(arg);
                if(loop_input){
                    JLM_ASSERT(*loop_input == nullptr);
                    *loop_input = arg->input();
                }
                return false;
            }
            bool snbb = false;
            for (size_t i = 1; i < node->ninputs(); ++i) {
                snbb |= should_not_be_buffered(node->input(i)->origin(), loop_input);
            }
            return snbb;
        } else if (auto sg = dynamic_cast<const jlm::hls::state_gate_op *>(&node->operation())){
//            bool snbb = false;
//            for (size_t i = 0; i < node->ninputs(); ++i) {
//                snbb |= should_not_be_buffered(node->input(i)->origin(), loop_input);
//            }
//            return snbb;
            return should_not_be_buffered(node->input(so->index())->origin(), loop_input);
        } else {
//            bool snbb = false;
//            for (size_t i = 0; i < node->ninputs(); ++i) {
//                snbb |= should_not_be_buffered(node->input(i)->origin(), loop_input);
//            }
//            return snbb;
        }
    } else if(auto be = dynamic_cast<jlm::hls::backedge_argument*>(o)){
        return should_not_be_buffered(be->result()->origin(), loop_input);
    } else if(auto sto = dynamic_cast<jive::structural_output*>(o)){
        jive::structural_input * sti = nullptr;
        if(should_not_be_buffered(sto->results.begin()->origin(), &sti)){
            return true;
        } else if(sti){
            return should_not_be_buffered(sti->origin(), loop_input);
        } else {
            JLM_UNREACHABLE("could not find loop input");
        }
    }
    JLM_UNREACHABLE("unexpected");
}

void place_buffer(jive::output * o, jive::structural_input ** loop_input= nullptr){
    if(breaks_cycle(o)){
        return;
    }
    if(!should_not_be_buffered(o)){
        // we can place a buffer here
        std::vector<jive::input *> old_users(o->begin(), o->end());
        auto new_buf = jlm::hls::buffer_op::create(*o, 2, false)[0];
        for (auto ou: old_users) {
            ou->divert_to(new_buf);
        }
        return;
    }
    if(auto so = dynamic_cast<jive::simple_output*>(o)){
        auto node = so->node();
        if (auto branch = dynamic_cast<const jlm::hls::branch_op *>(&node->operation())){
            return place_buffer(node->input(1)->origin(), loop_input);
        } else if (auto fork = dynamic_cast<const jlm::hls::fork_op *>(&node->operation())){
            // part of memory disambiguation
            return place_buffer(node->input(0)->origin(), loop_input);
        } else if (auto buffer = dynamic_cast<const jlm::hls::buffer_op *>(&node->operation())){
            if(buffer->pass_through){
                return place_buffer(node->input(0)->origin(), loop_input);
            }
        } else if (auto mux = dynamic_cast<const jlm::hls::mux_op *>(&node->operation())){
            if(mux->loop){
                auto arg = dynamic_cast<jive::argument*>(node->input(1)->origin());
                JLM_ASSERT(arg);
                if(loop_input){
                    JLM_ASSERT(*loop_input == nullptr);
                    *loop_input = arg->input();
                }
                return;
            }
            for (size_t i = 1; i < node->ninputs(); ++i) {
                place_buffer(node->input(i)->origin(), loop_input);
            }
            return;
        } else if (auto sg = dynamic_cast<const jlm::hls::state_gate_op *>(&node->operation())){
            for (size_t i = 0; i < node->ninputs(); ++i) {
                place_buffer(node->input(i)->origin(), loop_input);
            }
            return;
        }
    } else if(auto be = dynamic_cast<jlm::hls::backedge_argument*>(o)){
        return place_buffer(be->result()->origin(), loop_input);
    } else if(auto sto = dynamic_cast<jive::structural_output*>(o)){
        jive::structural_input * sti = nullptr;
        place_buffer(sto->results.begin()->origin(), &sti);
        if(sti){
            return place_buffer(sti->origin(), loop_input);
        } else {
            JLM_UNREACHABLE("could not find loop input");
        }
    }
    JLM_UNREACHABLE("unexpected");
}

void
jlm::hls::remove_redundant_buf(jive::region *region) {
	for (auto &node : jive::topdown_traverser(region)) {
        if (auto lambda = dynamic_cast<jlm::lambda::node*>(node)){
            remove_redundant_buf(lambda->subregion());
        } else if (auto loop_node = dynamic_cast<jlm::hls::loop_node *>(node)) {
            for (auto user: *loop_node->predicate()->origin()) {
                if(auto ni = dynamic_cast<jive::node_input*>(user)){
                    if(auto br = dynamic_cast<const jlm::hls::branch_op*>(&ni->node()->operation())){
                        JLM_ASSERT(br->loop);
                        auto br_node = ni->node();
                        auto no = br_node->output(1);
                        if(!jive::is<jlm::MemoryStateType>(no->type())){
                            continue;
                        }
                        JLM_ASSERT(no->nusers()==1);
                        auto user = *no->begin();
                        if(auto ni2 = dynamic_cast<jive::node_input*>(user)){
                            // there is a buffer that could be eliminated
                            auto buf = dynamic_cast<const jlm::hls::buffer_op*>(&ni2->node()->operation());
                            JLM_ASSERT(buf);
                            if(buf->pass_through || !should_not_be_buffered(no)){
                                continue;
                            }
                            auto new_buf = jlm::hls::buffer_op::create(*no, buf->capacity, true)[0];
                            ni2->node()->output(0)->divert_users(new_buf);
                            remove(ni2->node());
                            place_buffer(no);
                        }
                    }
                }
            }
            // handle inner loops afterwards, since we might introduce buffers in them
            remove_redundant_buf(loop_node->subregion());
        }
	}
}

void
jlm::hls::remove_redundant_buf(jlm::RvsdgModule &rm) {
	auto &graph = rm.Rvsdg();
	auto root = graph.root();
	remove_redundant_buf(root);
}