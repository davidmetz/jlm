//
// Created by david on 8/26/21.
//

#include <jlm/ir/hls/hls.h>
#include <jive/rvsdg/traverser.hpp>
#include <jlm/backend/hls/rvsdg2rhls/rvsdg2rhls.hpp>
#include "jlm/backend/hls/rvsdg2rhls/add-buffers.hpp"

void
jlm::hls::add_buffers(jive::region *region) {
	for (auto &node : jive::topdown_traverser(region)) {
		if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
			for (size_t n = 0; n < structnode->nsubregions(); n++) {
				add_buffers(structnode->subregion(n));
			}
		} else if (auto simplenode = dynamic_cast<jive::simple_node *>(node)) {
			if (jive::is<hls::buffer_op>(node)) {
				continue;
			} else if (is_constant(node)) {
				continue;
			} else if (jive::is<hls::print_op>(node)) {
				continue;
			}
			for (int i = 0; i < node->noutputs(); ++i) {
				auto out = node->output(i);
				JLM_ASSERT(out->nusers() == 1);
				if (auto ni = dynamic_cast<jive::node_input *>(*out->begin())) {
					if (jive::is<hls::buffer_op>(ni->node())) {
						continue;
					}
				}
				std::vector<jive::input *> old_users(out->begin(), out->end());
				auto new_out = buffer_op::create(*out, 1)[0];
				for (auto user: old_users) {
					user->divert_to(new_out);
				}
			}
		}
	}
}

void
jlm::hls::add_buffers(jlm::rvsdg_module &rm) {
	auto &graph = *rm.graph();
	auto root = graph.root();
	add_buffers(root);
}
