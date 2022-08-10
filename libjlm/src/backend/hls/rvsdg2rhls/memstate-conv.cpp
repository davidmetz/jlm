//
// Created by david on 7/2/21.
//

#include "jlm/backend/hls/rvsdg2rhls/memstate-conv.hpp"
#include "jlm/ir/operators/store.hpp"
#include "jlm/ir/operators/load.hpp"
#include "jlm/ir/operators/lambda.hpp"
#include <jive/rvsdg/substitution.hpp>
#include <jlm/ir/hls/hls.hpp>
#include <jive/rvsdg/theta.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <jlm/opt/alias-analyses/Operators.hpp>

void
jlm::hls::memstate_conv(jlm::RvsdgModule &rm) {
    auto &graph = rm.Rvsdg();
    auto root = graph.root();
    memstate_conv(root);
}

void jlm::hls::memstate_conv(jive::region *region) {
    for (auto &node: jive::topdown_traverser(region)) {
        if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
            for (size_t n = 0; n < structnode->nsubregions(); n++)
                memstate_conv(structnode->subregion(n));
        } else if (auto simplenode = dynamic_cast<jive::simple_node * >(node)) {
            if (dynamic_cast<const jlm::aa::LambdaEntryMemStateOperator *>(&simplenode->operation())) {
                auto new_outs = hls::fork_op::create(simplenode->noutputs(), *simplenode->input(0)->origin());
                for (size_t i = 0; i < simplenode->noutputs(); ++i) {
                    simplenode->output(i)->divert_users(new_outs[i]);
                }
                remove(simplenode);
            }
            // exit is handled as normal simple_op
        }
    }
}


