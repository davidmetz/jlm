//
// Created by david on 7/2/21.
//

#include "jlm/backend/hls/rvsdg2rhls/remove-unused-state.hpp"
#include <jive/rvsdg/traverser.hpp>

void
jlm::hls::remove_unused_state(jive::region *region, bool can_remove_arguments) {
	// process children first so that unnecessary users get removed
	for (auto &node : jive::topdown_traverser(region)) {
		if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
			if (auto gn = dynamic_cast<jive::gamma_node *>(node)) {
				// process subnodes first
				for (size_t n = 0; n < gn->nsubregions(); n++) {
					remove_unused_state(gn->subregion(n), false);
				}
				remove_gamma_passthrough(gn);
			} else if (auto ln = dynamic_cast<jlm::lambda::node *>(node)) {
				remove_unused_state(structnode->subregion(0), false);
				remove_lambda_passthrough(ln);
			} else {
				assert(structnode->nsubregions() == 1);
				remove_unused_state(structnode->subregion(0));
			}
		}
	}
	if (can_remove_arguments) {
		// check if an input is passed through unnecessarily
		for (int i = region->narguments() - 1; i >= 0; --i) {
			auto arg = region->argument(i);
			if (is_passthrough(arg)) {
				remove_region_passthrough(arg);
			}
		}
	}
}

void
jlm::hls::remove_unused_state(jlm::rvsdg_module &rm) {
	auto &graph = *rm.graph();
	auto root = graph.root();
	remove_unused_state(root);
}

void
jlm::hls::remove_gamma_passthrough(jive::gamma_node *gn) {// remove inputs in reverse
	for (int i = gn->nentryvars() - 1; i >= 0; --i) {
		bool can_remove = true;
		for (size_t n = 0; n < gn->nsubregions(); n++) {
			auto sr = gn->subregion(n);
			can_remove &= is_passthrough(sr->argument(i));
		}
		if (can_remove) {
			auto origin = gn->entryvar(i)->origin();
			// remove input
			gn->remove_input(i + 1);
			// divert users of output to origin of input
			auto res_index = dynamic_cast<jive::result *>(*gn->subregion(0)->argument(
					i)->begin())->index();
			gn->output(res_index)->divert_users(origin);
			gn->remove_output(res_index);
			for (int j = 0; j < gn->nsubregions(); ++j) {
				gn->subregion(j)->remove_result(res_index);
				gn->subregion(j)->remove_argument(i);
			}
		}
	}
}

jlm::lambda::node *
jlm::hls::remove_lambda_passthrough(jlm::lambda::node *ln) {
	auto old_fcttype = ln->type();
	std::vector<const jive::type *> new_argument_types;
	for (int i = 0; i < old_fcttype.narguments(); ++i) {
		auto arg = ln->subregion()->argument(i);
		auto argtype = &old_fcttype.argument_type(i);
		assert(*argtype == arg->type());
		if (!is_passthrough(arg)) {
			new_argument_types.push_back(argtype);
		}
	}
	std::vector<const jive::type *> new_result_types;
	for (int i = 0; i < old_fcttype.nresults(); ++i) {
		auto res = ln->subregion()->result(i);
		auto restype = &old_fcttype.result_type(i);
		assert(*restype == res->type());
		if (!is_passthrough(res)) {
			new_result_types.push_back(&old_fcttype.result_type(i));
		}
	}
	jive::fcttype new_fcttype(new_argument_types, new_result_types);
	auto new_lambda = jlm::lambda::node::create(ln->region(), new_fcttype, ln->name(), ln->linkage(),
												ln->attributes());

	jive::substitution_map smap;
	for (int i = 0; i < ln->ncvarguments(); ++i) {
		// copy over cvarguments
		smap.insert(ln->cvargument(i), new_lambda->add_ctxvar(ln->cvargument(i)->input()->origin()));
	}
	int new_i = 0;
	for (int i = 0; i < ln->nfctarguments(); ++i) {
		auto arg = ln->fctargument(i);
		if (!is_passthrough(arg)) {
			smap.insert(arg, new_lambda->fctargument(new_i));
			new_i++;
		}
	}
	ln->subregion()->copy(new_lambda->subregion(), smap, false, false);

	std::vector<jive::output *> new_results;
	for (int i = 0; i < ln->nfctresults(); ++i) {
		auto res = ln->fctresult(i);
		if (!is_passthrough(res)) {
			new_results.push_back(smap.lookup(res->origin()));
		}
	}
	auto new_out = new_lambda->finalize(new_results);

	// TODO handle functions at other levels?
	assert(ln->region() == ln->region()->graph()->root());
	assert((*ln->output()->begin())->region() == ln->region()->graph()->root());

//	ln->output()->divert_users(new_out); // can't divert since the type changed
	JLM_ASSERT(ln->output()->nusers()==1);
	ln->region()->remove_result((*ln->output()->begin())->index());
	remove(ln);
	jive::result::create(new_lambda->region(), new_out, nullptr, new_out->type());
	return new_lambda;
}

void
jlm::hls::remove_region_passthrough(const jive::argument *arg) {
	auto res = dynamic_cast<jive::result *>(*arg->begin());
	auto origin = arg->input()->origin();
	// divert users of output to origin of input
	arg->region()->node()->output(res->output()->index())->divert_users(origin);
	// remove result first so argument has no users
	arg->region()->remove_result(res->index());
	arg->region()->remove_argument(arg->index());
	arg->region()->node()->remove_input(arg->input()->index());
	arg->region()->node()->remove_output(res->output()->index());
}

bool
jlm::hls::is_passthrough(const jive::result *res) {
	auto arg = dynamic_cast<jive::argument *>(res->origin());
	if (arg) {
		return true;
	}
	return false;
}

bool
jlm::hls::is_passthrough(const jive::argument *arg) {
	if (arg->nusers() == 1) {
		auto res = dynamic_cast<jive::result *>(*arg->begin());
		// used only by a result
		if (res) {
			return true;
		}
	}
	return false;
}
