/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#include "jlm/backend/hls/rvsdg2rhls/merge-gamma.hpp"
#include <jive/rvsdg/substitution.hpp>
#include <jlm/ir/hls/hls.hpp>
#include <jive/rvsdg/theta.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <jive/rvsdg/region.hpp>

void
jlm::hls::merge_gamma(jlm::RvsdgModule &rm) {
	auto &graph = rm.Rvsdg();
	auto root = graph.root();
    merge_gamma(root);
}

void
jlm::hls::merge_gamma(jive::region *region) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto &node: jive::topdown_traverser(region)) {
            if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
                for (size_t n = 0; n < structnode->nsubregions(); n++)
                    merge_gamma(structnode->subregion(n));
                if (auto gamma = dynamic_cast<jive::gamma_node *>(node)) {
                    changed = changed != merge_gamma(gamma);
                }
            }
        }
    }
}

bool is_output_of(jive::output * output, jive::node * node){
    auto no = dynamic_cast<jive::node_output*>(output);
    return no && no->node()==node;
}

bool depends_on(jive::output * output, jive::node * node){
    auto arg = dynamic_cast<jive::argument*>(output);
    if(arg){
        return false;
    }
    auto no = dynamic_cast<jive::node_output*>(output);
    JLM_ASSERT(no);
    if(no->node()==node){
        return true;
    }
    for (size_t i = 0; i < no->node()->ninputs(); ++i) {
        if(depends_on(no->node()->input(i)->origin(), node)){
            return true;
        }
    }
    return false;
}

jive::gamma_input * get_entryvar(jive::output * origin, jive::gamma_node *gamma){
    for (auto user: *origin) {
        auto gi = dynamic_cast<jive::gamma_input*>(user);
        if(gi && gi->node() == gamma){
            return gi;
        }
    }
    return gamma->add_entryvar(origin);
}

bool jlm::hls::merge_gamma(jive::gamma_node *gamma) {
    for(auto user: *gamma->predicate()->origin()){
        auto gi = dynamic_cast<jive::gamma_input*>(user);
        if(gi && gi!=gamma->predicate()){
            // other gamma depending on same predicate
            auto other_gamma = gi->node();
            JLM_ASSERT(other_gamma->nsubregions() == gamma->nsubregions());
            bool can_merge = true;
            for (size_t i = 0; i < gamma->nentryvars(); ++i) {
                auto ev = gamma->entryvar(i);
                // we only merge gammas whose inputs directly, or not at all, depend on the gamma being merged into
                can_merge &= is_output_of(ev->origin(), other_gamma) || !depends_on(ev->origin(), other_gamma);
            }
            for (size_t i = 0; i < other_gamma->nentryvars(); ++i) {
                auto oev = other_gamma->entryvar(i);
                // prevent cycles
                can_merge &= !depends_on(oev->origin(), gamma);
            }
            if(can_merge){
                std::vector<jive::substitution_map> rmap(gamma->nsubregions());
                // populate argument mappings
                for (size_t i = 0; i < gamma->nentryvars(); ++i) {
                    auto ev = gamma->entryvar(i);
                    if(is_output_of(ev->origin(), other_gamma)){
                        auto go = dynamic_cast<jive::gamma_output*>(ev->origin());
                        for (size_t j = 0; j < gamma->nsubregions(); ++j) {
                            rmap[j].insert(ev->argument(j), go->result(j)->origin());
                        }
                    } else {
                        auto oev = get_entryvar(ev->origin(), other_gamma);
                        for (size_t j = 0; j < gamma->nsubregions(); ++j) {
                            rmap[j].insert(ev->argument(j), oev->argument(j));
                        }
                    }
                }
                // copy subregions
                for (size_t j = 0; j < gamma->nsubregions(); ++j) {
                    gamma->subregion(j)->copy(other_gamma->subregion(j), rmap[j], false, false);
                }
                // handle exitvars
                for (size_t i = 0; i < gamma->nexitvars(); ++i) {
                    auto ex = gamma->exitvar(i);
                    std::vector<jive::output*> operands;
                    for (size_t j = 0; j < ex->nresults(); j++){
                        operands.push_back(rmap[j].lookup(ex->result(j)->origin()));
                    }
                    auto oex = other_gamma->add_exitvar(operands);
                    ex->divert_users(oex);
                }
                remove(gamma);
                return true;
            }
        }
    }
    return false;
}
