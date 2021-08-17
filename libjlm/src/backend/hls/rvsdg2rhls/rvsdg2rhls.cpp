//
// Created by david on 7/2/21.
//

#include <jlm/backend/hls/rvsdg2rhls/rvsdg2rhls.hpp>
#include <jive/rvsdg/substitution.hpp>
#include <jlm/backend/hls/rvsdg2rhls/remove-unused-state.hpp>
#include <jlm/backend/hls/rvsdg2rhls/add-triggers.hpp>
#include <jlm/backend/hls/rvsdg2rhls/gamma-conv.hpp>
#include <jlm/backend/hls/rvsdg2rhls/theta-conv.hpp>
#include <jlm/backend/hls/rvsdg2rhls/add-sinks.hpp>
#include <jlm/backend/hls/rvsdg2rhls/add-forks.hpp>
#include <jlm/backend/hls/rvsdg2rhls/rhls-dne.hpp>
#include <jlm/opt/dne.hpp>
#include <jlm/opt/invariance.hpp>
#include <jlm/opt/inversion.hpp>
#include <jlm/opt/cne.hpp>
#include <jlm/util/stats.hpp>
#include <jlm/backend/hls/rvsdg2rhls/check-rhls.hpp>
#include <jlm/ir/rvsdg-module.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <regex>
#include <jlm/ir/operators/delta.hpp>

void
pre_opt(jlm::rvsdg_module &rm) {
	// TODO: figure out which optimizations to use here
	jlm::dne dne;
	jlm::cne cne;
	jlm::ivr ivr;
	jlm::tginversion tgi;
	jlm::stats_descriptor sd;
	tgi.run(rm, sd);
	dne.run(rm, sd);
	cne.run(rm, sd);
	ivr.run(rm, sd);
}

namespace jlm {

	bool function_match(lambda::node * ln, std::string &function_name){
		const std::regex fn_regex(function_name);
		if(std::regex_match(ln->name(), fn_regex)){// TODO: handle C++ name mangling
			return true;
		}
		return false;
	}

	std::unique_ptr<jlm::rvsdg_module>
	split_hls_function(jlm::rvsdg_module &rm, std::string &function_name) {
		// TODO: use a different datastructure for rhls?
		// create a copy of rm
		auto rhls = jlm::rvsdg_module::create(rm.source_filename(), rm.target_triple(), rm.data_layout());
		std::cout << "processing " << rm.source_filename().name() << "\n";
		auto root = rm.graph()->root();
		for (auto node : jive::topdown_traverser(root)) {
			if (auto ln = dynamic_cast<lambda::node *>(node)) {
				if (!function_match(ln, function_name)) {
					continue;
				}
				// may not use other functions TODO: do inlining + global variables here
				jive::substitution_map smap;
				for (int i = 0; i < ln->ninputs(); ++i) {
					//TODO: check for imports
					auto orig_node = dynamic_cast<jive::node_output*>(ln->input(i)->origin())->node();
					if(auto oln = dynamic_cast<lambda::node*>(orig_node)){
						throw jlm::error("Inlining of function "+oln->name()+" not supported");
					} else if(auto odn = dynamic_cast<delta_node*>(orig_node)){
						std::cout << "delta node " << odn->name() << ": " << odn->type().debug_string() << "\n";
						// add import for delta to rhls
						impport im(odn->type(), odn->name(), linkage::external_linkage);
						auto arg = rhls->graph()->add_import(im);
						smap.insert(ln->input(i)->origin(), arg);
						// add export for delta to rm
						// TODO: check if not already exported and maybe adjust linkage?
						rm.graph()->add_export(odn->output(0), {odn->type(), odn->name()});
					} else{
						throw jlm::error("Unsupported node type: "+orig_node->operation().debug_string());
					}
//					std::cout << "cvarg " << i << ": " <<ln->input(i)->type().debug_string() << "\n";
				}
				// copy function into rhls
				auto new_ln = ln->copy(rhls->graph()->root(), smap);
				jive::result::create(rhls->graph()->root(), new_ln->output(), nullptr, new_ln->output()->type());
				// add function as input to rm and remove it
				impport im(ln->output()->type(), ln->name(),  linkage::external_linkage); //TODO: change linkage?
				auto arg = rm.graph()->add_import(im);
				ln->output()->divert_users(arg);
				remove(ln);
				std::cout << "function " << new_ln->name() << " extracted for HLS\n";
				return rhls;
			} else{
//				std::cout << "other node: " << node->operation().debug_string() <<"\n";
			}
		}
		throw jlm::error("HLS function "+function_name+" not found");
	}
}

std::unique_ptr<jlm::rvsdg_module>
jlm::hls::rvsdg2rhls(jlm::rvsdg_module &rm, std::string &function_name) {
	auto rhls = jlm::split_hls_function(rm, function_name); //TODO: pass through function name

	pre_opt(*rhls);

	// run conversion on copy
	jlm::hls::remove_unused_state(*rhls);
	// main conversion steps
	jlm::hls::add_triggers(*rhls); // TODO: make compatible with loop nodes?
	jlm::hls::gamma_conv(*rhls);
	jlm::hls::theta_conv(*rhls);
	// rhls optimization
	jlm::hls::dne(*rhls);
	// enforce 1:1 input output relationship
	jlm::hls::add_sinks(*rhls);
	jlm::hls::add_forks(*rhls);
	// ensure that all rhls rules are met
	jlm::hls::check_rhls(*rhls);
	return rhls;
}
