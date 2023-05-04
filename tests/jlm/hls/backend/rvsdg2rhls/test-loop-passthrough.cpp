/*
 * Copyright 2018 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include "test-registry.hpp"
#include "test-types.hpp"
#include "jlm/ir/hls/hls.hpp"
#include "jlm/backend/hls/rhls2firrtl/dot-hls.hpp"
#include "jlm/backend/hls/rvsdg2rhls/rhls-dne.hpp"

#include <jive/rvsdg/control.hpp>
#include <jive/rvsdg/gamma.hpp>
#include <jive/view.hpp>

#include <jlm/backend/llvm/rvsdg2jlm/rvsdg2jlm.hpp>
#include <jlm/ir/cfg-structure.hpp>
#include <jlm/ir/ipgraph-module.hpp>
#include <jlm/ir/operators.hpp>
#include <jlm/ir/print.hpp>
#include <jlm/ir/RvsdgModule.hpp>
#include <jlm/util/Statistics.hpp>


static void
stringToFile(
        std::string output,
        std::string fileName)
{
    std::ofstream outputFile;
    outputFile.open(fileName);
    outputFile << output;
    outputFile.close();
}

static int
test()
{
    using namespace jlm;

    jive::bittype bt1(1);
    FunctionType ft({&bt1, &jive::bit8, &jive::bit8}, {&jive::bit8});

    RvsdgModule rm(filepath(""), "", "");
    auto nf = rm.Rvsdg().node_normal_form(typeid(jive::operation));
    nf->set_mutable(false);

    /* setup graph */

    auto lambda = lambda::node::create(rm.Rvsdg().root(), ft, "f", linkage::external_linkage);

    auto loop = hls::loop_node::create(lambda->subregion());

    auto loop_out = loop->add_loopvar(lambda->fctargument(1));

    auto f = lambda->finalize({loop_out});
    rm.Rvsdg().add_export(f, {f->type(), ""});

    jive::view(rm.Rvsdg(), stdout);
    hls::DotHLS dhls;
    stringToFile(
            dhls.run(rm),
            "/tmp/jlm_hls_test_before.dot");

    hls::dne(rm);
    hls::DotHLS dhls2;
    stringToFile(
            dhls2.run(rm),
            "/tmp/jlm_hls_test_after.dot");

    // The whole loop gets eliminated, leading to a direct connection
    assert(lambda->fctresult(0)->origin() == lambda->fctargument(1));

	return 0;
}

JLM_UNIT_TEST_REGISTER("libjlm/backend/hls/rvsdg2rhls/test-loop-passthrough", test)
