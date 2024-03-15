/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#include <math.h>

#include <jlm/ir/hls/hls.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <jlm/ir/operators/load.hpp>
#include <jlm/ir/operators/store.hpp>
#include <jlm/ir/operators/call.hpp>
#include <jive/rvsdg/gamma.hpp>
#include <jive/rvsdg/theta.hpp>
#include <jlm/ir/operators.hpp>
#include "jlm/backend/hls/rvsdg2rhls/instrument-ref.hpp"
#include "jlm/backend/hls/rvsdg2rhls/add-prints.hpp"
#include "jlm/backend/hls/rhls2firrtl/base-hls.hpp"


namespace jlm::hls {
    lambda::node *change_function_name(lambda::node *ln, const std::string &name) {
        auto lambda = lambda::node::create(ln->region(), ln->type(), name, ln->linkage(), ln->attributes());

        /* add context variables */
        jive::substitution_map subregionmap;
        for (auto &cv: ln->ctxvars()) {
            auto origin = cv.origin();
            auto newcv = lambda->add_ctxvar(origin);
            subregionmap.insert(cv.argument(), newcv);
        }

        /* collect function arguments */
        for (size_t n = 0; n < ln->nfctarguments(); n++) {
            lambda->fctargument(n)->set_attributes(ln->fctargument(n)->attributes());
            subregionmap.insert(ln->fctargument(n), lambda->fctargument(n));
        }

        /* copy subregion */
        ln->subregion()->copy(lambda->subregion(), subregionmap, false, false);

        /* collect function results */
        std::vector<jive::output *> results;
        for (auto &result: ln->fctresults())
            results.push_back(subregionmap.lookup(result.origin()));

        /* finalize lambda */
        lambda->finalize(results);

        divert_users(ln, outputs(lambda));
        jive::remove(ln);

        return lambda;
    }

    void
    instrument_ref(jlm::RvsdgModule &rm) {
        auto &graph = rm.Rvsdg();
        auto root = graph.root();
        auto lambda = dynamic_cast<jlm::lambda::node *>(root->nodes.begin().ptr());
        change_function_name(lambda, "instrumented_ref");
        //TODO: make this less hacky by using the correct state types
        // addr, width, memstate
        jlm::FunctionType loadFunctionType(
            {jlm::PointerType::Create().get(), &jive::bit64, MemoryStateType::Create().get()},
            {MemoryStateType::Create().get(), loopstatetype::create().get()}
        );
       impport load_imp(loadFunctionType, "reference_load", linkage::external_linkage);
        auto reference_load = graph.add_import(load_imp);
        // addr, data, width, memstate
        jlm::FunctionType storeFunctionType(
            {jlm::PointerType::Create().get(), &jive::bit64, &jive::bit64, MemoryStateType::Create().get()},
            {MemoryStateType::Create().get(), loopstatetype::create().get()}
        );
        impport store_imp(storeFunctionType, "reference_store", linkage::external_linkage);
        auto reference_store = graph.add_import(store_imp);
        // addr, size, memstate
        jlm::FunctionType allocaFunctionType(
            {jlm::PointerType::Create().get(), &jive::bit64, MemoryStateType::Create().get()},
            {MemoryStateType::Create().get(), loopstatetype::create().get()}
        );
        impport alloca_imp(allocaFunctionType, "reference_alloca", linkage::external_linkage);
        auto reference_alloca = graph.add_import(alloca_imp);
        instrument_ref(root, reference_load, loadFunctionType, reference_store, storeFunctionType, reference_alloca, allocaFunctionType);
    }

    void
    instrument_ref(
        jive::region * region,
        jive::output * load_func,
        jlm::FunctionType & loadFunctionType,
        jive::output * store_func,
        jlm::FunctionType & storeFunctionType,
        jive::output * alloca_func,
        jlm::FunctionType & allocaFunctionType)
    {
        load_func = route_to_region(load_func, region);
        store_func = route_to_region(store_func, region);
        alloca_func = route_to_region(alloca_func, region);
        jlm::PointerType void_ptr;
        for (auto &node: jive::topdown_traverser(region)) {
            if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
                for (size_t n = 0; n < structnode->nsubregions(); n++) {
                    instrument_ref(structnode->subregion(n), load_func, loadFunctionType, store_func, storeFunctionType, alloca_func, allocaFunctionType);
                }
            } else if (auto lo = dynamic_cast<const jlm::LoadOperation *>(&(node->operation()))) {
                jive::output *addr = node->input(0)->origin();
                auto pt = dynamic_cast<const jlm::PointerType *>(&addr->type());
                JLM_ASSERT(pt);
                size_t bitWidth = jlm::hls::BaseHLS::JlmSize(&lo->GetLoadedType());
                int log2Bytes = log2(bitWidth / 8);
                auto width = jive::create_bitconstant(region, 64, log2Bytes);

                // Does this IF make sense now when the void_ptr doesn't have a type?
                if (addr->type() != void_ptr) {
                    addr = jlm::bitcast_op::create(addr, void_ptr);
                }
                auto memstate = node->input(1)->origin();
                auto new_memstate = jlm::CallNode::Create(load_func, loadFunctionType, {addr, width, memstate})[0];
                node->input(1)->divert_to(new_memstate);
            }  else if (auto ao = dynamic_cast<const jlm::alloca_op *>(&(node->operation()))) {
                // ensure that the size is one
                JLM_ASSERT(node->ninputs() == 1);
                auto constant_output = dynamic_cast<jive::node_output*>(node->input(0)->origin());
                JLM_ASSERT(constant_output);
                auto constant_operation = dynamic_cast<const jive::bitconstant_op*>(&constant_output->node()->operation());
                JLM_ASSERT(constant_operation);
                JLM_ASSERT(constant_operation->value().to_uint() == 1);
                jive::output *addr = node->output(0);
                // ensure that the alloca is an array type
                auto pt = dynamic_cast<const jlm::PointerType*>(&addr->type());
                JLM_ASSERT(pt);
                auto at = dynamic_cast<const jlm::arraytype*>(&ao->value_type());
                JLM_ASSERT(at);
                auto size = jive::create_bitconstant(region, 64, BaseHLS::JlmSize(at)/8);

                // Does this IF make sense now when the void_ptr doesn't have a type?
                if (addr->type() != void_ptr) {
                    addr = jlm::bitcast_op::create(addr, void_ptr);
                }
                std::vector<jive::input*> old_users(node->output(1)->begin(), node->output(1)->end());
                auto memstate = node->output(1);
                auto new_memstate = jlm::CallNode::Create(alloca_func, allocaFunctionType, {addr, size, memstate})[0];
                for (auto ou: old_users) {
                    ou->divert_to(new_memstate);
                }
            } else if (auto so = dynamic_cast<const jlm::StoreOperation *>(&(node->operation()))) {
                jive::output *addr = node->input(0)->origin();
                auto pt = dynamic_cast<const jlm::PointerType *>(&addr->type());
                JLM_ASSERT(pt);
                auto bt = dynamic_cast<const jive::bittype *>(&so->GetStoredType());
                JLM_ASSERT(bt);
                auto bitWidth = bt->nbits();
                int log2Bytes = log2(bitWidth / 8);
                auto width = jive::create_bitconstant(region, 64, log2Bytes);

                // Does this IF make sense now when the void_ptr doesn't have a type?
                if (addr->type() != void_ptr) {
                    addr = jlm::bitcast_op::create(addr, void_ptr);
                }
                auto data = node->input(1)->origin();
                auto dbt = dynamic_cast<const jive::bittype *>(&data->type());
                if (*dbt != jive::bit64) {
                    jlm::zext_op op(dbt->nbits(), 64);
                    data = jive::simple_node::create_normalized(data->region(), op, {data})[0];
                }
                auto memstate = node->input(2)->origin();
                auto new_memstate = jlm::CallNode::Create(store_func, storeFunctionType, {addr, data, width, memstate})[0];
                node->input(2)->divert_to(new_memstate);
            }
        }
    }
}
