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

bool eliminate_gamma_ctl(jive::gamma_node *gamma) {
    // eliminates gammas that just replicate the ctl input
    bool changed = false;
    for (size_t i = 0; i < gamma->noutputs(); ++i) {
        auto o = gamma->output(i);
        if(dynamic_cast<const jive::ctltype*>(&o->type())){
            bool eliminate = true;
            for (size_t j = 0; j < gamma->nsubregions(); ++j) {
                auto r = gamma->subregion(j)->result(i);
                if(auto so = dynamic_cast<jive::simple_output*>(r->origin())){
                    if(auto ctl = dynamic_cast<const jive::ctlconstant_op*>(&so->node()->operation())){
                        if(j == ctl->value().alternative()){
                            continue;
                        }
                    }
                }
                eliminate = false;
            }
            if(eliminate){
                if(o->nusers()){
                    o->divert_users(gamma->predicate()->origin());
                    changed = true;
                }
            }
        }
    }
    return changed;
}

bool fix_match_inversion(jive::gamma_node *old_gamma) {
    // inverts match and swaps regions for gammas that contain swapped control constants
    if(old_gamma->nsubregions()!=2){
        return false;
    }
    bool swapped = false;
    size_t ctl_cnt = 0;
    for (size_t i = 0; i < old_gamma->noutputs(); ++i) {
        auto o = old_gamma->output(i);
        if(dynamic_cast<const jive::ctltype*>(&o->type())){
            ctl_cnt++;
            swapped = true;
            for (size_t j = 0; j < old_gamma->nsubregions(); ++j) {
                auto r = old_gamma->subregion(j)->result(i);
                if(auto so = dynamic_cast<jive::simple_output*>(r->origin())){
                    if(auto ctl = dynamic_cast<const jive::ctlconstant_op*>(&so->node()->operation())){
                        if(j != ctl->value().alternative()){
                            continue;
                        }
                    }
                }
                swapped = false;
            }
        }
    }
    if(ctl_cnt!=1 || !swapped){
        return false;
    }
    if(auto no = dynamic_cast<jive::node_output*>(old_gamma->predicate()->origin())){
        if(no->nusers() != 1){
            return false;
        }
        if(auto match = dynamic_cast<const jive::match_op*>(&no->node()->operation())){
            if(match->nalternatives() == 2){
                uint64_t default_alternative = match->default_alternative()?0:1;
                jive::match_op op(match->nbits(), {{0, match->alternative(1)}, {1, match->alternative(0)}}, default_alternative, match->nalternatives());
                auto new_match = jive::simple_node::create_normalized(no->region(), op, {no->node()->input(0)->origin()})[0];
                auto new_gamma = jive::gamma_node::create(new_match, match->nalternatives());
                jive::substitution_map rmap0; // subregion 0 of the new gamma - 1 of the old
                jive::substitution_map rmap1;
                for (auto oev = old_gamma->begin_entryvar(); oev != old_gamma->end_entryvar(); oev++) {
                    auto nev = new_gamma->add_entryvar(oev->origin());
                    rmap0.insert(oev->argument(1), nev->argument(0));
                    rmap1.insert(oev->argument(0), nev->argument(1));
                }
                /* copy subregions */
                old_gamma->subregion(0)->copy(new_gamma->subregion(1), rmap1, false, false);
                old_gamma->subregion(1)->copy(new_gamma->subregion(0), rmap0, false, false);

                for (auto oex = old_gamma->begin_exitvar(); oex != old_gamma->end_exitvar(); oex++) {
                    std::vector<jive::output*> operands;
                    operands.push_back(rmap0.lookup(oex->result(1)->origin()));
                    operands.push_back(rmap1.lookup(oex->result(0)->origin()));
                    auto nex = new_gamma->add_exitvar(operands);
                    oex.output()->divert_users(nex);
                }
                remove(old_gamma);
                remove(no->node());
                return true;
            }
        }
    }
    return false;
}

bool eliminate_gamma_eol(jive::gamma_node *gamma) {
    // eliminates gammas that are only active at the end of the loop and have unused outputs
    // seems to be mostly loop variables
    auto theta = dynamic_cast<jive::theta_node*>(gamma->region()->node());
    if(!theta || theta->predicate()->origin() != gamma->predicate()->origin()){
        return false;
    }
    if(gamma->nsubregions() != 2){
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < gamma->noutputs(); ++i) {
        auto o = gamma->output(i);
        if(o->nusers() != 1){
            continue;
        }
        auto user = *o->begin();
        if(auto res = dynamic_cast<jive::result*>(user)){
            if(res->output() && res->output()->nusers() == 0){
                // continue loop subregion
                if(auto arg = dynamic_cast<jive::argument*>(gamma->subregion(1)->result(i)->origin())){
                    // value is just passed through
                    if(o->nusers()){
                        o->divert_users(arg->input()->origin());
                        changed = true;
                    }
                }
            }
        }
    }
    return changed;
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
                    if(
                        fix_match_inversion(gamma)
                        || eliminate_gamma_ctl(gamma)
                        || eliminate_gamma_eol(gamma)
                        || merge_gamma(gamma)
                    ){
                        changed = true;
                        break;
                    }
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
