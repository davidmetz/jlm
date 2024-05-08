/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#include <jlm/ir/hls/hls.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <jlm/backend/hls/rvsdg2rhls/rvsdg2rhls.hpp>
#include "jlm/backend/hls/rvsdg2rhls/add-buffers.hpp"
#include "jive/types/bitstring/arithmetic.hpp"

bool traces_to_req(jive::output * out){
    auto user = *out->begin();
    if(auto ni = dynamic_cast<jive::node_input*>(user)){
        if(jive::is<jlm::hls::mem_req_op>(ni->node())){
            return true;
        }
        return false;
    } else if(auto res = dynamic_cast<jive::result*>(user)){
        if(res->output()){
            return traces_to_req(res->output());
        }
        // backedge
        return false;
    } else {
        return false;
    }
}

void
jlm::hls::add_buffers(jive::region *region, bool pass_through) {
	for (auto &node : jive::topdown_traverser(region)) {
		if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
			for (size_t n = 0; n < structnode->nsubregions(); n++) {
				add_buffers(structnode->subregion(n), pass_through);
			}
            if(auto loop_node = dynamic_cast<jlm::hls::loop_node*>(node)){
                // decouple subsequent outer loops from each other
                for (size_t i = 0; i < node->noutputs(); ++i) {
                    auto out = node->output(i);
                    JLM_ASSERT(out->nusers() == 1);
                    if(traces_to_req(out)){
                        continue;
                    }
                    if(dynamic_cast<jlm::hls::loop_node*>(loop_node->region()->node())){
                        // only buffer after outer loops
                        continue;
                    }
                    std::vector<jive::input *> users(out->begin(), out->end());
                    auto new_out = buffer_op::create(*out, 1, false)[0];
                    for(auto user: users){
                        user->divert_to(new_out);
                    }
                }
            }
		} else if (dynamic_cast<jive::simple_node *>(node)) {
            if(
                    jive::is<hls::load_op>(node)||
                    jive::is<hls::decoupled_load_op>(node)
                    ){
                auto out = node->output(0);
                JLM_ASSERT(out->nusers() == 1);
                if (auto ni = dynamic_cast<jive::node_input *>(*out->begin())) {
                    auto buf = dynamic_cast<const hls::buffer_op*>(&ni->node()->operation());
                    if (buf && (buf->pass_through||!pass_through)) {
                        continue;
                    }
                }
                std::vector<jive::input *> old_users(out->begin(), out->end());
                jive::output* new_out;
                if(pass_through){
                    new_out = buffer_op::create(*out, 2, true)[0];
                } else{
                    new_out = buffer_op::create(*out, 1, false)[0];
                }
                for (auto user: old_users) {
                    user->divert_to(new_out);
                }
            }
            auto fo = dynamic_cast<const fork_op*>(&node->operation());
            if (
                    (fo && !fo->constant)
//                    ||jive::is<hls::state_gate_op>(node)
                ) {
                for (size_t i = 0; i < node->noutputs(); ++i) {
                    auto out = node->output(i);
                    JLM_ASSERT(out->nusers() == 1);
                    if (auto ni = dynamic_cast<jive::node_input *>(*out->begin())) {
                        auto buf = dynamic_cast<const hls::buffer_op *>(&ni->node()->operation());
                        if (buf && (buf->pass_through || !pass_through)) {
                            continue;
                        }
                    }
                    std::vector<jive::input *> old_users(out->begin(), out->end());
                    jive::output *new_out;
                    if (pass_through) {
                        if(dynamic_cast<const jive::ctltype*>(&node->input(0)->type())){
//                            new_out = buffer_op::create(*out, 2, true)[0];
                            new_out = buffer_op::create(*out, 4, true)[0];
                        }else{
//                            new_out = buffer_op::create(*out, 1, true)[0];
                            new_out = buffer_op::create(*out, 2, true)[0];
                        }
                    } else {
                        new_out = buffer_op::create(*out, 1, false)[0];
                    }
                    for (auto user: old_users) {
                        user->divert_to(new_out);
                    }
                }
            }
            if (jive::is<hls::state_gate_op>(node)){
                auto out = node->output(0);
                if (auto ni = dynamic_cast<jive::node_input *>(*out->begin())) {
                    if(jive::is<jlm::hls::decoupled_load_op>(ni->node())){
                        ni->divert_to(buffer_op::create(*out, 1, true)[0]);
                    }
                }
            }
            if (dynamic_cast<const jive::bitmul_op *>(&(node->operation()))) {
                bool has_constant_input = false;
                for (size_t j = 0; j < node->ninputs(); ++j) {
                    if (auto no = dynamic_cast<jive::node_output *>(node->input(j)->origin())) {
                        auto constant = dynamic_cast<const jive::bitconstant_op *>(&no->node()->operation());
                        auto fork = dynamic_cast<const hls::fork_op *>(&no->node()->operation());
                        if (constant || (fork && fork->constant)) {
                            has_constant_input = true;
                            break;
                        }
                    }
                }
                if(has_constant_input){
                    continue;
                }
                auto out = node->output(0);
                std::vector<jive::input *> old_users(out->begin(), out->end());
                jive::output *new_out = buffer_op::create(*out, 2, false)[0];
                for (auto user: old_users) {
                    user->divert_to(new_out);
                }
            }
		}
	}
}

void
jlm::hls::add_buffers(jlm::RvsdgModule &rm, bool pass_through) {
	auto &graph = rm.Rvsdg();
	auto root = graph.root();
	add_buffers(root, pass_through);
}
