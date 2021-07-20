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

std::unique_ptr<jlm::rvsdg_module>
jlm::hls::rvsdg2rhls(jlm::rvsdg_module &rm) {
	// TODO: use a different datastructure for rhls?
	// create a copy of rm
	auto rhls = rvsdg_module::create(rm.source_filename(), rm.target_triple(), rm.data_layout());
	auto graph = rhls->graph();
	jive::substitution_map smap;
	rm.graph()->root()->copy(graph->root(), smap, true, true);

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
