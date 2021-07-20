//
// Created by david on 7/2/21.
//

#include <jlm/ir/operators/lambda.hpp>
#include <jive/rvsdg/traverser.hpp>
#include "jlm/backend/hls/rvsdg2rhls/rhls-dne.hpp"

bool
jlm::hls::remove_unused_loop_outputs(hls::loop_node *ln) {
	bool any_changed = false;
	auto sr = ln->subregion();
	// go through in reverse because we remove some
	for (int i = ln->noutputs() - 1; i >= 0; --i) {
		auto out = ln->output(i);
		if (out->nusers() == 0) {
			assert(out->results.size() == 1);
			auto result = out->results.begin();
			sr->remove_result(result->index());
			ln->remove_output(out->index());
			any_changed = true;
		}
	}
	return any_changed;
}

bool
jlm::hls::remove_unused_loop_inputs(hls::loop_node *ln) {
	bool any_changed = false;
	auto sr = ln->subregion();
	// go through in reverse because we remove some
	for (int i = ln->ninputs() - 1; i >= 0; --i) {
		auto in = ln->input(i);
		assert(in->arguments.size() == 1);
		auto arg = in->arguments.begin();
		if (arg->nusers() == 0) {
			sr->remove_argument(arg->index());
			ln->remove_input(in->index());
			any_changed = true;
		}
	}
	// clean up unused arguments - only ones without an input should be left
	// go through in reverse because we remove some
	int res_i = sr->nresults() - 1;
	for (int i = sr->narguments() - 1; i >= 0; --i) {
		auto arg = sr->argument(i);
		if (arg->input() != nullptr) {
			assert(arg->nusers() != 0);
		} else {
			// not all loop vars have outputs due to remove_unused_loop_io
			while (sr->result(res_i)->output() != nullptr) {
				res_i--;
			}
			auto result = sr->result(res_i);
			assert(result->type() == arg->type());
			res_i--;
			if (arg->nusers() == 0) {
				sr->remove_argument(arg->index());
				sr->remove_result(result->index());
			}
		}
	}
	return any_changed;
}

bool
jlm::hls::dne(jive::region *sr) {
	bool any_changed = false;
	bool changed;
	do {
		changed = false;
		for (auto &node : jive::bottomup_traverser(sr)) {
			if (!node->has_users()) {
				remove(node);
				changed = true;
			} else if (auto ln = dynamic_cast<hls::loop_node *>(node)) {
				changed |= remove_unused_loop_outputs(ln);
				changed |= remove_unused_loop_inputs(ln);
				changed |= dne(ln->subregion());
			}
		}
		any_changed |= changed;
	} while (changed);
	assert(sr->bottom_nodes.empty());
	return any_changed;
}

void
jlm::hls::dne(jlm::rvsdg_module &rm) {
	auto &graph = *rm.graph();
	auto root = graph.root();
	if (root->nodes.size() != 1) {
		throw jlm::error("Root should have only one node now");
	}
	auto ln = dynamic_cast<const jlm::lambda::node *>(root->nodes.begin().ptr());
	if (!ln) {
		throw jlm::error("Node needs to be a lambda");
	}
	dne(ln->subregion());
}
