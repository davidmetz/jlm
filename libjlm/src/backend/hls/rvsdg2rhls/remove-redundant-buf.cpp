/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#include "jlm/backend/hls/rvsdg2rhls/remove-redundant-buf.hpp"
#include "jlm/opt/alias-analyses/Operators.hpp"
#include "jlm/ir/hls/hls.hpp"
#include <jive/rvsdg/traverser.hpp>
#include <jive/view.hpp>

bool eliminate_buf(jive::output * o){
    if(auto so = dynamic_cast<jive::simple_output*>(o)){
        auto node = so->node();
        if (auto branch = dynamic_cast<const jlm::hls::branch_op *>(&node->operation())){
            return eliminate_buf(node->input(1)->origin());
        } else if (auto local_load = dynamic_cast<const jlm::hls::local_load_op *>(&node->operation())){
            return true;
        }
    }
    return false;
}

void
jlm::hls::remove_redundant_buf(jive::region *region) {
	for (auto &node : jive::topdown_traverser(region)) {
		if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
            for (size_t n = 0; n < structnode->nsubregions(); n++) {
                remove_redundant_buf(structnode->subregion(n));
            }
		} else if (auto simplenode = dynamic_cast<jive::simple_node *>(node)) {
            if (auto buf = dynamic_cast<const jlm::hls::buffer_op *>(&node->operation())){
                if(dynamic_cast<const jlm::MemoryStateType*>(&buf->argument(0).type())){
                    if(!buf->pass_through && eliminate_buf(node->input(0)->origin())){
                        auto new_out = jlm::hls::buffer_op::create(*node->input(0)->origin(), buf->capacity, true)[0];
                        node->output(0)->divert_users(new_out);
                        remove(node);
                    }
                }
            }
        }
	}
}

void
jlm::hls::remove_redundant_buf(jlm::RvsdgModule &rm) {
	auto &graph = rm.Rvsdg();
	auto root = graph.root();
	remove_redundant_buf(root);
}