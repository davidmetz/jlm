/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#include <jlm/backend/hls/rvsdg2rhls/rvsdg2rhls.hpp>
#include <jive/rvsdg/substitution.hpp>
#include <jlm/backend/hls/rvsdg2rhls/remove-unused-state.hpp>
#include <jlm/backend/hls/rvsdg2rhls/add-triggers.hpp>
#include <jlm/backend/hls/rvsdg2rhls/gamma-conv.hpp>
#include <jlm/backend/hls/rvsdg2rhls/theta-conv.hpp>
#include <jlm/backend/hls/rvsdg2rhls/add-sinks.hpp>
#include <jlm/backend/hls/rvsdg2rhls/add-forks.hpp>
#include <jlm/backend/hls/rvsdg2rhls/rhls-dne.hpp>
#include <jlm/opt/InvariantValueRedirection.hpp>
#include <jlm/opt/inversion.hpp>
#include <jlm/opt/cne.hpp>
#include <jlm/backend/hls/rvsdg2rhls/check-rhls.hpp>
#include <jlm/ir/RvsdgModule.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <jlm/ir/operators/delta.hpp>
#include <jlm/backend/hls/rvsdg2rhls/add-prints.hpp>
#include <jlm/backend/hls/rvsdg2rhls/alloca-conv.hpp>
#include <jlm/backend/llvm/rvsdg2jlm/rvsdg2jlm.hpp>
#include <jlm/backend/llvm/jlm2llvm/jlm2llvm.hpp>

#include <regex>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/SourceMgr.h>
#include <jlm/ir/operators/call.hpp>
#include <jlm/ir/operators/alloca.hpp>
#include <jlm/opt/inlining.hpp>
#include <jlm/ir/operators/theta.hpp>
#include <jlm/util/Statistics.hpp>
#include "jlm/opt/DeadNodeElimination.hpp"
#include "jlm/opt/reduction.hpp"
#include "jive/view.hpp"
#include "jlm/backend/hls/rvsdg2rhls/mem-conv.hpp"
#include "jlm/backend/hls/rvsdg2rhls/mem-sep.hpp"
#include "jlm/backend/hls/rvsdg2rhls/memstate-conv.hpp"
#include "jlm/backend/hls/rvsdg2rhls/dae-conv.hpp"
#include "jlm/backend/hls/rvsdg2rhls/remove-redundant-buf.hpp"
#include "jlm/backend/hls/rvsdg2rhls/add-buffers.hpp"
#include "jlm/backend/hls/rvsdg2rhls/mem-queue.hpp"
#include <jlm/opt/alias-analyses/Optimization.hpp>

void
split_opt(jlm::RvsdgModule &rm) {
	// TODO: figure out which optimizations to use here
	jlm::DeadNodeElimination dne;
	jlm::cne cne;
	jlm::InvariantValueRedirection ivr;
	jlm::tginversion tgi;
    jlm::nodereduction red;
    jlm::StatisticsDescriptor sd;
	tgi.run(rm, sd);
	dne.run(rm, sd);
	cne.run(rm, sd);
	ivr.run(rm, sd);
    red.run(rm, sd);
    dne.run(rm, sd);
}

void
pre_opt(jlm::RvsdgModule &rm) {
	// TODO: figure out which optimizations to use here
	jlm::DeadNodeElimination dne;
	jlm::cne cne;
	jlm::InvariantValueRedirection ivr;
	jlm::tginversion tgi;
    jlm::aa::SteensgaardBasic aa;
	jlm::StatisticsDescriptor sd;
	tgi.run(rm, sd);
	dne.run(rm, sd);
	cne.run(rm, sd);
	ivr.run(rm, sd);
    dne.run(rm, sd);
//    aa.run(rm, sd);
}

void
dump_xml(jlm::RvsdgModule &rvsdgModule, const std::string &file_name) {
    auto xml_file = fopen(file_name.c_str(), "w");
    jive::view_xml(rvsdgModule.Rvsdg().root(), xml_file);
    fclose(xml_file);
}

namespace jlm {

	bool
	function_match(lambda::node *ln, const std::string &function_name) {
		const std::regex fn_regex(function_name);
		if (std::regex_match(ln->name(), fn_regex)) {// TODO: handle C++ name mangling
			return true;
		}
		return false;
	}

	inline bool
	is_fct_ptr(jive::input *input) {
		auto pt = dynamic_cast<const jlm::PointerType *>(&input->type());
		if (!pt)
			return false;
		return dynamic_cast<const FunctionType *>(&pt->GetElementType());
	}

    const jive::output *hls::trace_call(jive::input *input) {
        JLM_ASSERT(is_fct_ptr(input));

		auto graph = input->region()->graph();

        auto argument = dynamic_cast<const jive::argument *>(input->origin());
        const jive::output * result;
        if (auto to = dynamic_cast<const jive::theta_output*>(input->origin())){
            result = trace_call(to->input());
        } else if (argument == nullptr) {
            result = input->origin();
        } else if (argument->region() == graph->root()){
            result = argument;
        } else{
            JLM_ASSERT(argument->input() != nullptr);
            result = trace_call(argument->input());
        }
        return result;
    }

	void
	inline_calls(jive::region *region) {
		for (auto &node : jive::topdown_traverser(region)) {
			if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
				for (size_t n = 0; n < structnode->nsubregions(); n++) {
					inline_calls(structnode->subregion(n));
				}
			} else if (dynamic_cast<const jlm::CallOperation *>(&(node->operation()))) {
				auto traced = jlm::hls::trace_call(node->input(0));
                auto so = dynamic_cast<const jive::structural_output *>(traced);
                if(!so){
                    auto arg = dynamic_cast<const jive::argument *>(traced);
                    auto ip = dynamic_cast<const impport *>(&arg->port());
                    if(ip){
                        if(ip->name().rfind("decouple_", 0)==0){
                            // can't inline pseudo functions used for decoupling
                            continue;
                        }
                        throw jive::compiler_error("can not inline external function "+ip->name());
                    }
                }
                JLM_ASSERT(is<lambda::operation>(so->node()));
				auto ln = dynamic_cast<const jive::structural_output *>(traced)->node();
				jlm::inlineCall(dynamic_cast<jive::simple_node *>(node), dynamic_cast<const lambda::node *>(ln));
				// restart for this region
				inline_calls(region);
				return;
			}
		}
	}

	size_t alloca_cnt = 0;

	void
	convert_alloca(jive::region *region) {
		for (auto &node : jive::topdown_traverser(region)) {
			if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
				for (size_t n = 0; n < structnode->nsubregions(); n++) {
					convert_alloca(structnode->subregion(n));
				}
			} else if (auto po = dynamic_cast<const jlm::alloca_op *>(&(node->operation()))) {
				auto rr = region->graph()->root();
				auto delta_name = jive::detail::strfmt("hls_alloca_", alloca_cnt++);
				auto delta_type = jlm::PointerType(po->value_type());
                std::cout << "alloca " << delta_name << ": " << po->value_type().debug_string() << "\n";
				auto db = delta::node::Create(rr, delta_type, delta_name, linkage::external_linkage, "", false);
				// create zero constant of allocated type
				jive::output *cout;
				if (auto bt = dynamic_cast<const jive::bittype *>(&po->value_type())) {
					cout = jive::create_bitconstant(db->subregion(), bt->nbits(), 0);
				} else {
					ConstantAggregateZero cop(po->value_type());
					cout = jive::simple_node::create_normalized(db->subregion(), cop, {})[0];
				}
				auto delta = db->finalize(cout);
				region->graph()->add_export(delta, {delta_type, delta_name});
				auto delta_local = jlm::hls::route_to_region(delta, region);
				node->output(0)->divert_users(delta_local);
				// TODO: check that the input to alloca is a bitconst 1
                // TODO: handle general case of other nodes getting state edge without a merge
                JLM_ASSERT(node->output(1)->nusers() == 1);
				auto mux_in = *node->output(1)->begin();
				auto mux_node = input_node(mux_in);
				if(dynamic_cast<const jlm::MemStateMergeOperator *>(&mux_node->operation())){
                    // merge after alloca -> remove merge
                    JLM_ASSERT(mux_node->ninputs() == 2);
                    auto other_index = mux_in->index() ? 0 : 1;
                    mux_node->output(0)->divert_users(mux_node->input(other_index)->origin());
                    jive::remove(mux_node);
                } else {
                    //TODO: fix this properly by adding a state edge to the LambdaEntryMemState and routing it to the region
                    JLM_ASSERT(false);
                }
				jive::remove(node);
			}
		}
	}

	delta::node *
	rename_delta(delta::node *odn) {
		auto name = odn->name();
		std::replace_if(name.begin(), name.end(), [](char c) { return c == '.'; }, '_');
		std::cout << "renaming delta node " << odn->name() << " to " << name << "\n";
		auto db = delta::node::Create(odn->region(), odn->type(), name, linkage::external_linkage, "", odn->constant());
		/* add dependencies */
		jive::substitution_map rmap;
		for (size_t i=0; i<odn->ncvarguments(); i++) {
			auto input = odn->input(i);
			auto nd = db->add_ctxvar(input->origin());
			rmap.insert(input->argument(), nd);
		}

		/* copy subregion */
		odn->subregion()->copy(db->subregion(), rmap, false, false);

		auto result = rmap.lookup(odn->subregion()->result(0)->origin());
		auto data = db->finalize(result);

		odn->output()->divert_users(data);
		jive::remove(odn);
		return static_cast<delta::node *>(jive::node_output::node(data));
	}

    lambda::node * change_linkage(lambda::node * ln, linkage link){
        auto lambda = lambda::node::create(ln->region(), ln->type(), ln->name(), link, ln->attributes());

        /* add context variables */
        jive::substitution_map subregionmap;
        for (auto & cv : ln->ctxvars()) {
            auto origin = cv.origin();
            auto newcv = lambda->add_ctxvar(origin);
            subregionmap.insert(cv.argument(), newcv);
        }

        /* collect function arguments */
        for (size_t n = 0; n < ln->nfctarguments(); n++){
            lambda->fctargument(n)->set_attributes(ln->fctargument(n)->attributes());
            subregionmap.insert(ln->fctargument(n), lambda->fctargument(n));
        }

        /* copy subregion */
        ln->subregion()->copy(lambda->subregion(), subregionmap, false, false);

        /* collect function results */
        std::vector<jive::output*> results;
        for (auto & result : ln->fctresults())
            results.push_back(subregionmap.lookup(result.origin()));

        /* finalize lambda */
        lambda->finalize(results);

        divert_users(ln,outputs(lambda));
        jive::remove(ln);

        return lambda;
    }
}

std::unique_ptr<jlm::RvsdgModule>
jlm::hls::split_hls_function(jlm::RvsdgModule &rm, const std::string &function_name) {
    // TODO: use a different datastructure for rhls?
    // create a copy of rm
    auto rhls = jlm::RvsdgModule::Create(rm.SourceFileName(), rm.TargetTriple(), rm.DataLayout());
    std::cout << "processing " << rm.SourceFileName().name() << "\n";
    auto root = rm.Rvsdg().root();
    for (auto node : jive::topdown_traverser(root)) {
        if (auto ln = dynamic_cast<lambda::node *>(node)) {
            if (!function_match(ln, function_name)) {
                continue;
            }
            inline_calls(ln->subregion());
            dump_xml(rm, "post_inline.rvsdg");
            split_opt(rm);
            dump_xml(rm, "post_opt.rvsdg");
//            convert_alloca(ln->subregion());
            jive::substitution_map smap;
            for (size_t i = 0; i < ln->ninputs(); ++i) {
                auto orig_node_output = dynamic_cast<jive::node_output *>(ln->input(i)->origin());
                if(!orig_node_output){
                    // handle decouple stuff
                    auto arg = dynamic_cast<const jive::argument *>(ln->input(i)->origin());
                    auto ip = dynamic_cast<const impport *>(&arg->port());
                    auto new_arg = rhls->Rvsdg().add_import(*ip);
                    smap.insert(ln->input(i)->origin(), new_arg);
                    continue;
                }
                auto orig_node = orig_node_output->node();
                if (auto oln = dynamic_cast<lambda::node *>(orig_node)) {
                    throw jlm::error("Inlining of function " + oln->name() + " not supported");
                } else if (auto odn = dynamic_cast<delta::node *>(orig_node)) {
                    // modify name to not contain .
                    if (odn->name().find('.') != std::string::npos) {
                        odn = rename_delta(odn);
                    }
                    std::cout << "delta node " << odn->name() << ": " << odn->type().debug_string() << "\n";
                    // add import for delta to rhls
                    impport im(odn->type(), odn->name(), linkage::external_linkage);
//						JLM_ASSERT(im.name()==odn->name());
                    auto arg = rhls->Rvsdg().add_import(im);
                    auto tmp = dynamic_cast<const impport *>(&arg->port());
                    assert(tmp && tmp->name() == odn->name());
                    smap.insert(ln->input(i)->origin(), arg);
                    // add export for delta to rm
                    // TODO: check if not already exported and maybe adjust linkage?
                    rm.Rvsdg().add_export(odn->output(), {odn->type(), odn->name()});
                } else {
                    throw jlm::error("Unsupported node type: " + orig_node->operation().debug_string());
                }
            }
            // copy function into rhls
            auto new_ln = ln->copy(rhls->Rvsdg().root(), smap);
            new_ln = change_linkage(new_ln, linkage::external_linkage);
            jive::result::create(rhls->Rvsdg().root(), new_ln->output(), nullptr, new_ln->output()->type());
            // add function as input to rm and remove it
            impport im(ln->output()->type(), ln->name(), linkage::external_linkage); //TODO: change linkage?
            auto arg = rm.Rvsdg().add_import(im);
            ln->output()->divert_users(arg);
            remove(ln);
            std::cout << "function " << new_ln->name() << " extracted for HLS\n";
            return rhls;
        }
    }
    throw jlm::error("HLS function " + function_name + " not found");
}

void
jlm::hls::rvsdg2rhls(jlm::RvsdgModule &rhls) {
	pre_opt(rhls);

//	jlm::hls::add_prints(rhls);
//	dump_ref(rhls);

	// run conversion on copy
	jlm::hls::remove_unused_state(rhls);
    jlm::hls::mem_sep_argument(rhls);
    // run conversion on copy
    jlm::hls::remove_unused_state(rhls);
	// main conversion steps
//	jlm::hls::add_triggers(rhls); // TODO: is this needed?
	jlm::hls::gamma_conv(rhls, true);
	jlm::hls::theta_conv(rhls);
	// rhls optimization
	jlm::hls::dne(rhls);
    jlm::hls::alloca_conv(rhls);
    jlm::hls::mem_queue(rhls);
    jlm::hls::mem_conv(rhls);
    jlm::hls::memstate_conv(rhls);
    jlm::hls::mem_conv(rhls);
    jlm::hls::remove_redundant_buf(rhls);
	// enforce 1:1 input output relationship
	jlm::hls::add_sinks(rhls);
	jlm::hls::add_forks(rhls);
	jlm::hls::add_buffers(rhls, true);
	// ensure that all rhls rules are met
	jlm::hls::check_rhls(rhls);
}

void
jlm::hls::dump_ref(jlm::RvsdgModule &rhls) {
	auto reference = RvsdgModule::Create(rhls.SourceFileName(), rhls.TargetTriple(), rhls.DataLayout());
	jive::substitution_map smap;
	rhls.Rvsdg().root()->copy(reference->Rvsdg().root(), smap, true, true);
	convert_prints(*reference);
	for (size_t i = 0; i < reference->Rvsdg().root()->narguments(); ++i) {
		auto arg = reference->Rvsdg().root()->argument(i);
		auto imp = dynamic_cast<const impport *>(&arg->port());
		std::cout << "impport " << imp->name() << ": " << imp->type().debug_string() << "\n";
	}
	llvm::LLVMContext ctx;
	StatisticsDescriptor sd;
	auto jm2 = rvsdg2jlm::rvsdg2jlm(*reference, sd);
	auto lm2 = jlm2llvm::convert(*jm2, ctx);
	std::error_code EC;
	llvm::raw_fd_ostream os(reference->SourceFileName().base() + ".ref.ll", EC);
	lm2->print(os, nullptr);
}
