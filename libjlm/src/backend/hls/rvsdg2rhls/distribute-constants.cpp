/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#include "jlm/backend/hls/rvsdg2rhls/distribute-constants.hpp"
#include "jlm/backend/hls/util/view.hpp"
#include <jlm/ir/hls/hls.hpp>
#include <jive/rvsdg/gamma.hpp>
#include <jive/rvsdg/theta.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <jlm/backend/hls/rvsdg2rhls/rvsdg2rhls.hpp>

void distribute_constant(const jive::simple_op &op, jive::simple_output* out){
    assert(jlm::hls::is_constant(out->node()));
    bool changed = true;
    while(changed) {
        changed = false;
        for (auto user: *out) {
            if (auto ti = dynamic_cast<jive::theta_input *>(user)) {
                auto arg = ti->argument();
                auto res = ti->result();
                if (res->origin() == arg) {
                    // pass-through
                    auto arg_replacement = dynamic_cast<jive::simple_output *>(jive::simple_node::create_normalized(
                            ti->node()->subregion(), op, {})[0]);
                    ti->argument()->divert_users(arg_replacement);
                    ti->output()->divert_users(jive::simple_node::create_normalized(out->region(), op, {})[0]);
                    distribute_constant(op, arg_replacement);
                    arg->region()->remove_result(res->index());
                    arg->region()->remove_argument(arg->index());
                    arg->region()->node()->remove_input(arg->input()->index());
                    arg->region()->node()->remove_output(res->output()->index());
                    changed = true;
                    break;
                }
            }
            if (auto gi = dynamic_cast<jive::gamma_input *>(user)) {
                if (gi->node()->predicate() == gi) {
                    continue;
                }
                for (int i = gi->narguments() - 1; i >= 0; --i) {
                    if (gi->argument(i)->nusers()) {
                        auto arg_replacement = dynamic_cast<jive::simple_output *>(jive::simple_node::create_normalized(
                                gi->argument(i)->region(), op, {})[0]);
                        gi->argument(i)->divert_users(arg_replacement);
                        distribute_constant(op, arg_replacement);
                    }
                    gi->node()->subregion(i)->remove_argument(gi->argument(i)->index());
                }
                gi->node()->remove_input(gi->index());
                changed = true;
                break;
            }
        }
    }
}

void
jlm::hls::distribute_constants(jive::region *region) {
    //push constants down as far as possible, since this is cheaper than having forks and potentially buffers for them
	for (auto &node : jive::topdown_traverser(region)) {
		if (dynamic_cast<jive::structural_node *>(node)) {
			if (auto ln = dynamic_cast<jlm::lambda::node *>(node)) {
                distribute_constants(ln->subregion());
            } else if (auto t = dynamic_cast<jive::theta_node *>(node)) {
				distribute_constants(t->subregion());
			} else if (auto gn = dynamic_cast<jive::gamma_node *>(node)) {
				for (size_t i = 0; i < gn->nsubregions(); ++i) {
					distribute_constants(gn->subregion(i));
				}
			} else {
				throw jlm::error("Unexpected node type: " + node->operation().debug_string());
			}
		} else if (auto sn = dynamic_cast<jive::simple_node *>(node)) {
			if (is_constant(node)) {
                distribute_constant(sn->operation(), sn->output(0));
			}
		} else {
			throw jlm::error("Unexpected node type: " + node->operation().debug_string());
		}
	}
}

void
jlm::hls::distribute_constants(jlm::RvsdgModule &rm) {
	auto &graph = rm.Rvsdg();
	auto root = graph.root();
	distribute_constants(root);
}
