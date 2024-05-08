/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#include <jlm/ir/hls/hls.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <jlm/backend/hls/rvsdg2rhls/rvsdg2rhls.hpp>
#include "jlm/backend/hls/rvsdg2rhls/bram-pointers.hpp"
#include "jlm/ir/operators/lambda.hpp"
#include "jlm/ir/operators/operators.hpp"
#include "jlm/ir/operators/getelementptr.hpp"
#include "jlm/backend/hls/rvsdg2rhls/rhls-dne.hpp"
#include "jlm/backend/hls/rhls2firrtl/base-hls.hpp"
#include "jive/types/bitstring/arithmetic.hpp"

void replace_gep(jive::simple_node *simplenode) {
    auto op = dynamic_cast<const jlm::getelementptr_op *>(&simplenode->operation());

//    auto bc = jive::bitconstant_op::create(simplenode->region(), jive::bitvalue_repr(jlm::hls::pointer_bits, 0));
//    auto ptr = jlm::bits2ptr_op::create(bc, simplenode->input(0)->type());
//    simplenode->input(0)->divert_to(ptr);

    jive::output *out = nullptr;
    JLM_ASSERT(op);
    const jive::type *pointeeType = &op->pointee_type();
    for (size_t i = 1; i < simplenode->ninputs(); ++i) {
        int bits = jlm::hls::BaseHLS::JlmSize(pointeeType);
        int bytes = bits / 8;
        if (dynamic_cast<const jive::bittype *>(pointeeType)) {
            pointeeType = nullptr;
        } else if (auto arrayType = dynamic_cast<const jlm::arraytype *>(pointeeType)) {
            pointeeType = &arrayType->element_type();
        } else {
            throw std::logic_error(pointeeType->debug_string() + " pointer not implemented!");
        }
        //TODO: strength reduction to shift if applicable
        auto constant = jive::bitconstant_op::create(simplenode->region(),
                                                     jive::bitvalue_repr(jlm::hls::pointer_bits, bytes));
        auto ot = dynamic_cast<const jive::bittype *>(&simplenode->input(i)->origin()->type());
        auto origin = simplenode->input(i)->origin();
        JLM_ASSERT(ot);
        if (ot->nbits() != jlm::hls::pointer_bits) {
            origin = jlm::trunc_op::create(jlm::hls::pointer_bits, origin);
        }
        auto mul = jive::bitmul_op::create(jlm::hls::pointer_bits, constant, origin);
        if (out) {
            out = jive::bitadd_op::create(jlm::hls::pointer_bits, out, mul);
        } else {
            out = mul;
        }
    }
    JLM_ASSERT(out);
    auto ptr = jlm::bits2ptr_op::create(out, simplenode->output(0)->type());
    simplenode->output(0)->divert_users(ptr);
    remove(simplenode);
}

void replace_geps(jive::output *out) {
    JLM_ASSERT(dynamic_cast<const jlm::PointerType *>(&out->type()));
    bool changed;
    do {
        changed = false;
        for (auto user: *out) {
            if (auto sti = dynamic_cast<jive::structural_input *>(user)) {
                JLM_ASSERT(sti->arguments.size() == 1);
                auto arg = sti->arguments.first();
                JLM_ASSERT(arg->nusers() == 1);
                auto arg_user = *arg->begin();
                auto ni = dynamic_cast<jive::node_input *>(arg_user);
                JLM_ASSERT(ni);
                if (dynamic_cast<const jlm::hls::loop_constant_buffer_op *>(&ni->node()->operation())) {
                    replace_geps(ni->node()->output(0));
                }
            } else if (auto si = dynamic_cast<jive::simple_input *>(user)) {
                auto simplenode = si->node();
                if (dynamic_cast<const jlm::hls::branch_op *>(&simplenode->operation())) {
                    for (size_t i = 0; i < simplenode->noutputs(); ++i) {
                        replace_geps(simplenode->output(i));
                    }
                } else if (dynamic_cast<const jlm::getelementptr_op *>(&simplenode->operation())) {
                    replace_gep(simplenode);
                    // users of out change here
                    changed = true;
                    break;
                } else if (jive::is<jlm::hls::decoupled_load_op>(simplenode)
                           || jive::is<jlm::hls::load_op>(simplenode)
                           || jive::is<jlm::hls::store_op>(simplenode)
                        ) {
                    auto bc = jive::bitconstant_op::create(simplenode->region(), jive::bitvalue_repr(jlm::hls::pointer_bits, 0));
                    auto ptr = jlm::bits2ptr_op::create(bc, si->type());
                    si->divert_to(ptr);
                    changed = true;
                    break;
                } else {
                    auto bc = jive::bitconstant_op::create(simplenode->region(), jive::bitvalue_repr(jlm::hls::pointer_bits, 0));
                    auto ptr = jlm::bits2ptr_op::create(bc, si->type());
                    si->divert_to(ptr);
                    changed = true;
                    break;
//                    JLM_UNREACHABLE("Unexpected node type");
                }
            }
        }
    } while (changed);
}

void
jlm::hls::bram_pointers(jive::region *root) {
    // pass that removes unnecessary base pointers if brams are used
    auto lambda = dynamic_cast<jlm::lambda::node *>(root->nodes.begin().ptr());
    for (size_t i = 0; i < lambda->subregion()->narguments(); ++i) {
        auto arg = lambda->subregion()->argument(i);
        if (auto pt = dynamic_cast<const jlm::PointerType *>(&arg->type())) {
            if (dynamic_cast<const jlm::FunctionType *>(&pt->GetElementType())) {
                // skip function pointers
                continue;
            }
            replace_geps(arg);
            auto bc = jive::bitconstant_op::create(arg->region(), jive::bitvalue_repr(jlm::hls::pointer_bits, 0));
            auto ptr = jlm::bits2ptr_op::create(bc, *pt);
            arg->divert_users(ptr);
        }
    }
}

void
jlm::hls::bram_pointers(jlm::RvsdgModule &rm) {
    auto &graph = rm.Rvsdg();
    auto root = graph.root();
    bram_pointers(root);
    dne(rm);
}
