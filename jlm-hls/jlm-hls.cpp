/*
 * Copyright 2014 2015 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include <iostream>
#include <getopt.h>
#include <set>
#include <fstream>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/SourceMgr.h>

#include <jive/view.hpp>

#include <jlm/ir/ipgraph-module.hpp>
#include <jlm/ir/print.hpp>
#include <jlm/ir/rvsdg-module.hpp>
#include <jlm/util/stats.hpp>
#include <jlm/frontend/llvm/llvm2jlm/module.hpp>
#include <jlm/frontend/llvm/jlm2rvsdg/module.hpp>
#include <jlm/backend/hls/rvsdg2rhls/rvsdg2rhls.hpp>
#include <jlm/backend/hls/rhls2firrtl/dot-hls.hpp>
#include <jlm/backend/hls/rhls2firrtl/firrtl-hls.hpp>
#include <jlm/backend/hls/rhls2firrtl/verilator-harness-hls.hpp>
#include <jlm/backend/llvm/rvsdg2jlm/rvsdg2jlm.hpp>
#include <jlm/backend/llvm/jlm2llvm/jlm2llvm.hpp>

class cmdflags {
public:
    inline
    cmdflags() {}

    std::string file;
    jlm::stats_descriptor sd;
    std::string hls_function;
};

static void
print_usage(const std::string &app) {
    std::cerr << "Usage: " << app << " [OPTIONS]\n";
    std::cerr << "OPTIONS:\n";
    std::cerr << "--file name: LLVM IR file.\n";
    std::cerr << "--hls-function: name of the function to turn into an accelerator\n";
}

static void
parse_cmdflags(int argc, char **argv, cmdflags &flags) {
    static constexpr size_t file = 1;
    static constexpr size_t hls_function = 2;

    static struct option options[] = {
            {"file", required_argument, NULL, file},
            {"hls-function", required_argument, NULL, hls_function},
            {NULL, 0,                   NULL, 0}
    };

    int opt;
    while ((opt = getopt_long_only(argc, argv, "", options, NULL)) != -1) {
        switch (opt) {
            case file: {
                flags.file = optarg;
                break;
            }
            case hls_function: {
                flags.hls_function = optarg;
                break;
            }

            default:
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (flags.file.empty() || flags.hls_function.empty()) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
}
void
dump_xml(std::unique_ptr<jlm::rvsdg_module> &rm, const std::string &suffix = "") {
	auto source_file_name = rm->source_filename().name();
    std::string file_name = source_file_name.substr(0, source_file_name.find_last_of('.')) + suffix + ".rvsdg";
    auto xml_file = fopen(file_name.c_str(), "w");
    jive::view_xml(rm->graph()->root(), xml_file);
    fclose(xml_file);
}

int
main(int argc, char **argv) {
    cmdflags flags;
    parse_cmdflags(argc, argv, flags);

    llvm::LLVMContext ctx;
    llvm::SMDiagnostic err;
    auto lm = llvm::parseIRFile(flags.file, err, ctx);
    // TODO: fix this hack
    lm->setSourceFileName("jlm_hls");
    if (!lm) {
        err.print(argv[0], llvm::errs());
        exit(1);
    }

    /* LLVM to JLM pass */
    auto jm = jlm::convert_module(*lm);
//    jlm::print(*jm, stdout);
    auto rm = jlm::construct_rvsdg(*jm, flags.sd);

//    dump_xml(rm);
//    jive::view(rm->graph()->root(), stdout);
    auto rhls = jlm::hls::rvsdg2rhls(*rm, flags.hls_function);
//    jive::view(rhls->graph()->root(), stdout);
//    dump_xml(rhls, "_hls");	// run conversion on copy

	jlm::hls::FirrtlHLS hls;
    hls.run(*rhls);
	jlm::hls::DotHLS dhls;
    dhls.run(*rhls);
    jlm::hls::VerilatorHarnessHLS vhls;
    vhls.run(*rhls);

    // non-hls part
	auto jm2 = jlm::rvsdg2jlm::rvsdg2jlm(*rm, flags.sd);
	auto lm2 = jlm::jlm2llvm::convert(*jm2, ctx);
	std::error_code EC;
	auto source_file_name = lm->getSourceFileName();
	auto base_file_name = source_file_name.substr(0, source_file_name.find_last_of('.'));
	llvm::raw_fd_ostream os(base_file_name+".rest.ll", EC);
	lm2->print(os, nullptr);

    return 0;
}