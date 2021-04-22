/*
 * Copyright 2014 2015 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include <jlm/ir/ipgraph-module.hpp>
#include <jlm/ir/print.hpp>
#include <jlm/ir/rvsdg-module.hpp>
#include <jlm/ir/operators/lambda.hpp>
#include <jlm/llvm2jlm/module.hpp>
#include <jlm/jlm2llvm/jlm2llvm.hpp>
#include <jlm/jlm2rvsdg/module.hpp>
#include <jlm/rvsdg2jlm/rvsdg2jlm.hpp>
#include <jlm/util/stats.hpp>

#include <jive/rvsdg/graph.hpp>
#include <jive/rvsdg/gamma.hpp>
#include <jive/view.hpp>
#include <jive/types/function.hpp>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/SourceMgr.h>

#include <iostream>
#include <fstream>

#include <getopt.h>
#include <jlm/opt/dne.hpp>
#include <jive/rvsdg/control.hpp>
#include <jive/types/bitstring.hpp>
#include <jlm/ir/operators.hpp>
#include "../tests/test-operation.hpp"
#include "../tests/test-types.hpp"

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
    std::cerr << "--l2j: Print program after LLVM to JLM pass.\n";
    std::cerr << "--j2r: Print program after JLM to RVSDG pass.\n";
    std::cerr << "--r2j: Print program after RVSDG to JLM pass.\n";
    std::cerr << "--j2l: Print program after JLM to LLVM pass.\n";
    std::cerr << "--j2rx: Print program as XML after JLM to RVSDG pass.\n";
    std::cerr << "--l2jdot f: Print function f as graphviz after LLVM to JLM pass.\n";
    std::cerr << "--l2j-ipg-dot: Print inter-procedure graph after LLVM to JLM pass.\n";
    std::cerr << "--r2jdot f: Print function f as graphviz after RVSDG to JLM pass.\n";
    std::cerr << "--r2j-ipg-dot: Print inter-procedure graph after RVSDG to JLM pass.\n";
    std::cerr << "--file name: LLVM IR file.\n";
}

static void
parse_cmdflags(int argc, char **argv, cmdflags &flags) {
    static constexpr size_t file = 1;

    static struct option options[] = {
            {"file", required_argument, NULL, file},
            {NULL, 0,                   NULL, 0}
    };

    int opt;
    while ((opt = getopt_long_only(argc, argv, "", options, NULL)) != -1) {
        switch (opt) {
            case file: {
                flags.file = optarg;
                break;
            }

            default:
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (flags.file.empty()) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
}

static inline bool
is_identity_mapping(const jive::match_op &op) {
    for (const auto &pair : op) {
        if (pair.first != pair.second)
            return false;
    }

    return true;
}

class SimpleHLS {
public:
    SimpleHLS() {
    }

    void run(jlm::rvsdg_module &rm, llvm::Module *lm) {
        std::string firrtl;
        auto region = rm.graph()->root();
        for (const auto &node : region->nodes) {
            if (auto ln = dynamic_cast<const jlm::lambda::node *>(&node)) {
                auto module_name = lambda_node_to_string(ln);
                firrtl += indent(0) + "circuit " + module_name + ":\n";
            } else {
                std::cerr << "unknown node " << node.operation().debug_string() << "\n";
            }
        }
        for (auto module: modules) {
            firrtl += module;
        }
        auto source_file_name = lm->getSourceFileName();
        std::string file_name = source_file_name.substr(0, source_file_name.find_last_of('.')) + ".fir";
        std::ofstream firrtl_file;
        firrtl_file.open(file_name);
        firrtl_file << firrtl;
        firrtl_file.close();
    }

private:
    std::unordered_map<jive::output *, std::string> port_map;
    std::unordered_set<jive::output *> port_declared;
    std::vector<std::string> modules;

    std::string
    create_port_name(jive::output *port) {
        std::string name = dynamic_cast<const jive::argument *>(port) ? "a" : "o";
        name += jive::detail::strfmt(port_map.size());
        port_map[port] = name;
        return name;
    }

    std::string
    get_port_name(jive::input *port) {std::string result;
        if (auto p = dynamic_cast<const jive::node_input *>(port)) {
            result += "i";
        }
        result += jive::detail::strfmt(port->index());
        return result;
    }

    std::string
    get_port_name(jive::output *port) {
//        auto tmp = port_map.find(port);
//        std::string name;
//        if (tmp != port_map.end()) {
//            name = tmp->second;
//        } else {
//            name = create_port_name(port);
//        }
//        return name;
        if (port == nullptr) {
            throw std::logic_error("nullptr!");
        }
        std::string result;
        if (auto p = dynamic_cast<const jive::argument *>(port)) {
            result += "i";
        } else if (auto p = dynamic_cast<const jive::node_output *>(port)) {
            result += "o";
        } else if (auto p = dynamic_cast<const jive::structural_output *>(port)) {
            result += "so";
        } else {
            throw std::logic_error(port->debug_string() + " not implemented!");
        }
        result += jive::detail::strfmt(port->index());
        return result;
    }

    std::string
    to_firrtl_type(const jive::type *type) {
        if (auto t = dynamic_cast<const jive::bittype *>(type)) {
            return "UInt<" + jive::detail::strfmt(t->nbits()) + ">";
        } else if (auto t = dynamic_cast<const jive::statetype *>(type)) {
            return "UInt<1>";
        } else {
            throw std::logic_error(type->debug_string() + " not implemented!");
        }
    }
    std::string
    firrtl_assert(const std::string& condition, int depth=2) {
        std::string result;
        result += indent(depth) + "when not(or("+condition+",reset)):\n";
        result += indent(depth+1) + "stop(clk, UInt<1>(1), -1)\n";
        result += indent(depth+1) + "printf(clk, UInt<1>(1), \""+condition+"\")\n";
        return result;
    }

    std::string simple_node_to_string(const jive::simple_node *n) {
        std::string module_name;
        // ugly hack because debug string contains illegal symbols
        if (auto o = dynamic_cast<const jive::match_op *>(&(n->operation()))) {
            module_name += "MATCH_mod" + jive::detail::strfmt(modules.size());
        } else if (auto o = dynamic_cast<const jive::ctlconstant_op *>(&(n->operation()))) {
            module_name += "CTL_mod" + jive::detail::strfmt(modules.size());
        } else if (auto o = dynamic_cast<const jive::bitconstant_op *>(&(n->operation()))) {
            module_name += "BITS_mod" + jive::detail::strfmt(modules.size());
        } else {
            module_name += n->operation().debug_string() + "_mod" + jive::detail::strfmt(modules.size());
        }
        std::ostringstream module;
        module << indent(1) + "module " + module_name + ":\n";
        // io
        module << indent(2) + "; io" + "\n";
        module << indent(2) + "input clk: Clock" + "\n";
        module << indent(2) + "input reset: UInt<1>" + "\n";
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) << "input i" << (i) << ": {flip ready: UInt<1>, valid: UInt<1>, data: " <<
                    to_firrtl_type(&n->input(i)->type()) << "}\n";
        }
        for (size_t i = 0; i < n->noutputs(); ++i) {
            module << indent(2) + "output o" + jive::detail::strfmt(i) +
                      ": {flip ready: UInt<1>, valid: UInt<1>, data: " +
                      to_firrtl_type(&n->output(i)->type()) + "}" + "\n";
        }
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) << "assert(clk, not(and(i"<<i<<".valid, not(i"<<i<<".ready))), not(reset), \"valid only when ready\")\n";
//            module << firrtl_assert("not(and(i"+jive::detail::strfmt(i)+".valid, not(i"+jive::detail::strfmt(i)+".ready)))");
        }
        // registers
        module << indent(2) + "; registers" + "\n";
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) + "reg i" + jive::detail::strfmt(i) +
                      "_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))" + "\n";
            module << indent(2) + "reg i" + jive::detail::strfmt(i) + "_data_reg: " +
                      to_firrtl_type(&n->input(i)->type()) + ", clk" + "\n";
        }
        // logic
        assert(n->noutputs() == 1);
        auto out = n->output(0);
        module << indent(2) + "; logic" + "\n";
        module << indent(2) + get_port_name(out) + ".data <=";
        if (auto o = dynamic_cast<const jive::match_op *>(&(n->operation()))) {
            auto in0 = n->input(0);
            //TODO: this could be a pure pass through?
            if (is_identity_mapping(*o)) {
                // TODO: figure out why this hack is needed
//                module << get_port_name(in0) + "_data_reg";
                module << "i0_data_reg";
            } else {
                throw std::logic_error("non-identity mapping not implemented!");
            }
        } else if (auto o = dynamic_cast<const jive::bitsgt_op *>(&(n->operation()))) {
            auto in0 = n->input(0);
            auto in1 = n->input(1);
            module <<
                    "gt(asSInt(" + get_port_name(in0) + "_data_reg" + "), asSInt(" + get_port_name(in1) + "_data_reg" +
                    "))";
        } else if (auto o = dynamic_cast<const jive::bitugt_op *>(&(n->operation()))) {
            auto in0 = n->input(0);
            auto in1 = n->input(1);
            module << "gt(" + get_port_name(in0) + "_data_reg" + ", " + get_port_name(in1) + "_data_reg" + ")";
        } else if (auto o = dynamic_cast<const jive::bitult_op *>(&(n->operation()))) {
            auto in0 = n->input(0);
            auto in1 = n->input(1);
            module << "lt(" + get_port_name(in0) + "_data_reg" + ", " + get_port_name(in1) + "_data_reg" + ")";
        } else if (auto o = dynamic_cast<const jive::biteq_op *>(&(n->operation()))) {
            auto in0 = n->input(0);
            auto in1 = n->input(1);
            module << "eq(" + get_port_name(in0) + "_data_reg" + ", " + get_port_name(in1) + "_data_reg" + ")";
        } else if (auto o = dynamic_cast<const jive::bitadd_op *>(&(n->operation()))) {
            auto in0 = n->input(0);
            auto in1 = n->input(1);
            module << "add(" + get_port_name(in0) + "_data_reg" + ", " + get_port_name(in1) + "_data_reg" + ")";
        } else if (auto o = dynamic_cast<const jive::bitsub_op *>(&(n->operation()))) {
            auto in0 = n->input(0);
            auto in1 = n->input(1);
            module << "sub(" + get_port_name(in0) + "_data_reg" + ", " + get_port_name(in1) + "_data_reg" + ")";
        } else if (auto o = dynamic_cast<const jive::bitne_op *>(&(n->operation()))) {
            auto in0 = n->input(0);
            auto in1 = n->input(1);
            module << "neq(" + get_port_name(in0) + "_data_reg" + ", " + get_port_name(in1) + "_data_reg" + ")";
        } else if (auto o = dynamic_cast<const jive::bitconstant_op *>(&(n->operation()))) {
            auto value = o->value();
            module << "UInt<" << value.nbits() << ">(" << value.to_uint() << ")";
        } else if (auto o = dynamic_cast<const jlm::undef_constant_op *>(&(n->operation()))) {
            module << "UInt<1>(0) ; TODO: Fix?";
        } else if (auto o = dynamic_cast<const jive::ctlconstant_op *>(&(n->operation()))) {
            module << "UInt<1>(" + jive::detail::strfmt(o->value().alternative()) + ")";
        } else {
            throw std::logic_error(n->operation().debug_string() + " not implemented!");
        }
        module << " ; " + n->operation().debug_string() + "\n";
        // handshaking
        module << indent(2) + "; handshaking" + "\n";
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) + "i" + jive::detail::strfmt(i) + ".ready <= not(i" + jive::detail::strfmt(i) +
                      "_valid_reg)" + "\n";
        }
        module << indent(2) + "node inputs_valid = ";
        std::string inputs_valids = "UInt<1>(1)"; // True by default
        for (size_t i = 0; i < n->ninputs(); ++i) {
            inputs_valids = "and(" + inputs_valids + ", i" + jive::detail::strfmt(i) + "_valid_reg)";
        }
        module << inputs_valids + "\n";
        for (size_t i = 0; i < n->noutputs(); ++i) {
            module << indent(2) + "o" + jive::detail::strfmt(i) + ".valid <= and(o" + jive::detail::strfmt(i) +
                      ".ready, inputs_valid)" + "\n";
        }
        if (n->ninputs()) {
            module << indent(2) + "when o0.valid:" + "\n";
            for (size_t i = 0; i < n->ninputs(); ++i) {
                module << indent(3) + "i" + jive::detail::strfmt(i) + "_valid_reg <= UInt<1>(0)" + "\n";
            }
        }
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) + "when i" + jive::detail::strfmt(i) + ".valid:" + "\n";
            module << indent(3) + "i" + jive::detail::strfmt(i) + "_valid_reg <= UInt<1>(1)" + "\n";
            module << indent(3) + "i" + jive::detail::strfmt(i) + "_data_reg <= i" + jive::detail::strfmt(i) + ".data" +
                      "\n";
        }

        modules.push_back(module.str());
//        std::cerr << module;
        return module_name;
    }

    std::string gamma_node_to_string(const jive::gamma_node *n) {
        auto module_name = "gamma_mod" + jive::detail::strfmt(modules.size());
        auto module_index = modules.size();
        modules.emplace_back(); // need to preserve spot to avoid name collisions
        std::ostringstream module;
        module << indent(1) << "module " << module_name << ":\n";
        // io
        module << indent(2) + "; io" + "\n";
        module << indent(2) + "input clk: Clock" + "\n";
        module << indent(2) + "input reset: UInt<1>" + "\n";
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) << "input i" << i << ": {flip ready: UInt<1>, valid: UInt<1>, data: " <<
                   to_firrtl_type(&n->input(i)->type()) + "}" + "\n";
        }
        for (size_t i = 0; i < n->noutputs(); ++i) {
            module << indent(2) << "output o" << i << ": {flip ready: UInt<1>, valid: UInt<1>, data: " <<
                   to_firrtl_type(&n->output(i)->type()) << "}" << "\n";
        }
        // registers
        module << indent(2) << "; registers\n";
        module << indent(2) << "reg pred_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))\n";
        module << indent(2) << "reg pred_data_reg: " << to_firrtl_type(&n->predicate()->type()) << ", clk\n";
        module << indent(2) + "i0.ready <= not(pred_valid_reg)\n";
        for (size_t i = 0; i < n->nentryvars(); ++i) {
            module << indent(2) << "reg e" << i << "_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))\n";
            module << indent(2) << "reg  e" << i << "_data_reg: " << to_firrtl_type(&n->entryvar(i)->type())
                   << ", clk\n";
//            module << indent(2) << "wire e" << i <<"_ready\n";
//            module << indent(2) << "e" << i <<"_ready <= UInt<1>(0)\n";
            module << indent(2) + "i" << (i + 1) << ".ready <= not(e" << i << "_valid_reg)\n";
        }
        for (int i = 0; i < n->noutputs(); ++i) {
            module << indent(2) + "reg o" << i << "_processed_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))\n";
            module << indent(2) + "o" << i << ".valid <= " << "UInt<1>(0) ; off by default\n";
            module << indent(2) + "o" << i << ".data is invalid ; off by default\n";
        }

        module << indent(2) << "; instances\n";
        for (size_t k = 0; k < n->nsubregions(); ++k) {
            auto sr = n->subregion(k);
            auto sr_module_name = subregion_to_string(sr);
            module << indent(2) << "inst sr" << k << " of " + sr_module_name << "\n";
            module << indent(2) << "sr" << k << ".clk <= clk\n";
            module << indent(2) << "sr" << k << ".reset <= reset\n";
            module << indent(2) << "node pred_match_" << k << " = and(eq(pred_data_reg, UInt<32>(" << k
                   << ")), pred_valid_reg)\n";
            for (size_t i = 0; i < sr->narguments(); ++i) {
                module << indent(2) << "sr" << k << ".i" << i << ".valid <= and(and(e" << i << "_valid_reg, pred_match_"
                       << k << "), sr" << k << ".i" << i << ".ready)\n";
                module << indent(2) << "sr" << k << ".i" << i << ".data <= e" << i << "_data_reg\n";
            }

            for (int i = 0; i < n->noutputs(); ++i) {
                module << indent(2) << "sr" << (k) << ".o" << (i) << ".ready <= o" << (i) << ".ready\n";
            }
            module << indent(2) << "when pred_match_" << k << ":\n";
//            for (size_t i = 0; i < sr->narguments(); ++i) {
//                module << indent(3) << "e" << i <<"_ready <= sr" << k << ".i" << i << ".ready\n";
//            }
            for (int i = 0; i < n->noutputs(); ++i) {
                module << indent(3) << "o" << (i) << ".data <= sr" << (k) << ".o" << (i) << ".data\n";
                module << indent(3) << "o" << (i) << ".valid <= sr" << (k) << ".o" << (i) << ".valid\n";
            }
        }
        for (int i = 0; i < n->noutputs(); ++i) {
            module << indent(2) << "when o" << (i) << ".valid:\n";
            module << indent(3) << "o" << (i) << "_processed_reg <= UInt<1>(1)\n";
        }

        module << indent(2) << "when i0.valid:\n";
        module << indent(3) << "pred_valid_reg <= UInt<1>(1)\n";
        module << indent(3) << "pred_data_reg <= i0.data\n";

        for (size_t i = 0; i < n->nentryvars(); ++i) {
            module << indent(2) << "when i" << (i + 1) << ".valid:\n";
            module << indent(3) << "e" << i << "_valid_reg <= UInt<1>(1)\n";
            module << indent(3) << "e" << i << "_data_reg <= i" << (i + 1) << ".data\n";
        }

        std::string all_processed = "UInt<1>(1)"; // True by default
        for (size_t i = 0; i < n->nexitvars(); ++i) {
            all_processed = "and(" + all_processed + ", o" + jive::detail::strfmt(i) + "_processed_reg)";
        }
        module << indent(2) << "node all_processed = " << all_processed << "\n";
        module << indent(2) << "when all_processed:\n";
        module << indent(3) << "pred_valid_reg <= UInt<1>(0)\n";
        for (size_t i = 0; i < n->nentryvars(); ++i) {
            module << indent(3) << "e" << i << "_valid_reg <= UInt<1>(0)\n";
        }
        for (int i = 0; i < n->noutputs(); ++i) {
            module << indent(3) << "o" << (i) << "_processed_reg <= UInt<1>(0)\n";
        }

        // TODO: set pred and processed

        modules[module_index] = module.str();
        return module_name;
    }

    std::string theta_node_to_string(const int depth, const jive::theta_node *n) {
        auto module_name = "theta_mod" + jive::detail::strfmt(modules.size());
        auto module_index = modules.size();
        modules.emplace_back(); // need to preserve spot to avoid name collisions
        std::ostringstream module;
        auto sr = n->subregion();
        module << indent(1) << "module " << module_name << ":\n";
        // io
        module << indent(2) + "; io" + "\n";
        module << indent(2) + "input clk: Clock" + "\n";
        module << indent(2) + "input reset: UInt<1>" + "\n";
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) << "input i" << i << ": {flip ready: UInt<1>, valid: UInt<1>, data: " <<
                   to_firrtl_type(&n->input(i)->type()) + "}" + "\n";
        }
        for (size_t i = 0; i < n->noutputs(); ++i) {
            module << indent(2) << "output o" << i << ": {flip ready: UInt<1>, valid: UInt<1>, data: " <<
                   to_firrtl_type(&n->output(i)->type()) << "}\n";
        }
        // registers
        module << indent(2) << "; registers\n";
//        module << indent(2) << "reg looping_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))\n";
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) << "reg i" << i << "_processed_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))\n";
//            module << indent(2) + "reg i" << i << "_data_reg: "<< to_firrtl_type(&n->input(i)->type()) <<", clk\n";
        }
        for (size_t i = 0; i < n->noutputs(); ++i) {
            module << indent(2) << "reg o" << i << "_processed_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))\n";
        }
        for (int i = 0; i < sr->nresults(); ++i) {
            module << indent(2) + "reg o" << i << "_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))\n";
            module << indent(2) + "reg o" << i << "_data_reg: "<< to_firrtl_type(&sr->result(i)->type()) <<", clk\n";
        }
        // instances
        module << indent(2) << "; instances" << "\n";
        auto sr_module_name = subregion_to_string(sr, true);
        module << indent(2) << "inst sr of " << sr_module_name << "\n";
        module << indent(2) << "sr.clk <= clk\n";
        module << indent(2) << "sr.reset <= reset\n";
        for (size_t i = 0; i < sr->narguments(); ++i) {
            module << indent(2) << "sr.i" << (i) << ".valid <= i"<<i<<".valid\n";
            module << indent(2) << "sr.i" << (i) << ".data <= i"<<i<<".data\n";
            module << indent(2) << "i" << i << ".ready <= and(not(i" << i << "_processed_reg), sr.i"<< i <<".ready)\n";
        }

        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) << "when i" << (i) << ".valid:\n";
            module << indent(3) << "i" << i << "_processed_reg <= UInt<1>(1)\n";
        }

        for (size_t i = 0; i < sr->nresults(); ++i) {
            module << indent(2) << "when sr.o" << (i) << ".valid:\n";
            module << indent(3) << "o" << i << "_valid_reg <= UInt<1>(1)\n";
            if(i>=1)
                module << indent(3) << "o" << i-1 << "_processed_reg <= UInt<1>(1)\n";
            module << indent(3) << "o" << i << "_data_reg <= sr.o" << (i) << ".data\n";
            module << indent(2) << "sr.o" << i << ".ready <= not(o" << (i) << "_valid_reg)\n";
        }

        for (size_t i = 0; i < n->noutputs(); ++i) {
            module << indent(2) << "o" << i << ".data is invalid\n";
            module << indent(2) << "o" << i << ".valid <= UInt<1>(0)\n";
        }
        module << indent(2) << "when and(o0_valid_reg, not(o0_data_reg)): ; loop finished\n";
        for (size_t i = 0; i < n->noutputs(); ++i) {
            module << indent(3) << "o" << i << ".data <= o"<<(i+1)<<"_data_reg\n";
            module << indent(3) << "o" << i << ".valid <= and(o"<< (i) <<".ready, o"<<(i+1)<<"_valid_reg)\n";
            module << indent(3) << "when o" << i << ".valid:\n";
            module << indent(4) << "o" << (i+1) << "_valid_reg <= UInt<1>(0)\n";
        }
        module << indent(2) << "when and(o0_valid_reg, o0_data_reg): ; loop continuing\n";
        for (size_t i = 0; i < n->noutputs(); ++i) {
            module << indent(3) << "sr.i" << i << ".data <= o"<<(i+1)<<"_data_reg\n";
            module << indent(3) << "sr.i" << i << ".valid <= and(and(sr.i"<< (i) <<".ready, o"<<(i+1)<<"_valid_reg), not(o"<<i<<"_processed_reg))\n";
            module << indent(3) << "when sr.i" << (i) << ".valid:\n";
            module << indent(4) << "o" << (i+1) << "_valid_reg <= UInt<1>(0)\n";
        }

        std::string all_processed = "UInt<1>(1)"; // True by default
        for (size_t i = 0; i < n->noutputs(); ++i) {
            all_processed = "and(" + all_processed + ", o" + jive::detail::strfmt(i) + "_processed_reg)";
        }
        module << indent(2) << "node all_processed = " << all_processed << "\n";
        module << indent(2) << "when all_processed:\n";
        for (int i = 0; i < n->noutputs(); ++i) {
            module << indent(3) << "o" << (i) << "_processed_reg <= UInt<1>(0)\n";
        }
        module << indent(2) << "when and(all_processed, and(o0_valid_reg, not(o0_data_reg))):\n";
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(3) << "i" << i << "_processed_reg <= UInt<1>(0)\n";
        }
        module << indent(2) << "reg next_all_processed: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))\n";
        module << indent(2) << "next_all_processed <= all_processed\n";
        module << indent(2) << "when next_all_processed:\n";
        module << indent(3) << "o0_valid_reg <= UInt<1>(0)\n";



        modules[module_index] = module.str();
        return module_name;
    }

    std::string
    node_to_string(const jive::node *node, const int depth) {
        if (auto n = dynamic_cast<const jive::simple_node *>(node)) {
            return simple_node_to_string(n);
        } else if (auto n = dynamic_cast<const jive::gamma_node *>(node)) {
            return gamma_node_to_string(n);
        } else if (auto n = dynamic_cast<const jive::theta_node *>(node)) {
            return theta_node_to_string(depth, n);
        } else {
            throw std::logic_error(node->operation().debug_string() + " not implemented!");
        }
    }

    static inline std::string
    indent(size_t depth) {
        return std::string(depth * 4, ' ');
    }

    std::string
    create_dummy_module(const jive::type *type){
        std::string module_name = "dummy_mod" + jive::detail::strfmt(modules.size());
        std::string module = indent(1) + "module " + module_name + ":\n";
        // io
        module += indent(2) + "; io" + "\n";
        module += indent(2) + "input clk: Clock" + "\n";
        module += indent(2) + "input reset: UInt<1>" + "\n";
        module += indent(2) + "input i0: {flip ready: UInt<1>, valid: UInt<1>, data: " +
                to_firrtl_type(type) + "}" + "\n";
        module += indent(2) + "output o0: {flip ready: UInt<1>, valid: UInt<1>, data: " +
                  to_firrtl_type(type) + "}" + "\n";
        // registers
        module += indent(2) + "; registers" + "\n";
        module += indent(2) + "reg i0_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))" + "\n";
        module += indent(2) + "reg i0_data_reg: " + to_firrtl_type(type) + ", clk" + "\n";
        module += indent(2) + "; logic" + "\n";
        module += indent(2) + "o0.data <= i0_data_reg";
        module += indent(2) + "; handshaking" + "\n";
        module += indent(2) + "i0.ready <= not(i0_valid_reg)" + "\n";
        module += indent(2) + "node inputs_valid = i0_valid_reg";
        module += indent(2) + "o0.valid <= and(o0.ready, inputs_valid)" + "\n";
        module += indent(2) + "when o0.valid:" + "\n";
        module += indent(3) + "i0_valid_reg <= UInt<1>(0)" + "\n";
        module += indent(2) + "when i0.valid:" + "\n";
        module += indent(3) + "i0_valid_reg <= UInt<1>(1)" + "\n";
        module += indent(3) + "i0_data_reg <= i0.data" +"\n";
        modules.push_back(module);
        return module_name;
    }

    std::string
    subregion_to_string(const jive::region *sr, bool register_comb = false) {
        auto module_name = "subregion_mod" + jive::detail::strfmt(modules.size());
        auto module_index = modules.size();
        modules.emplace_back(); // need to preserve spot to avoid name collisions
        std::string module = indent(1) + "module " + module_name + ":\n";
        // io
        module += indent(2) + "; io" + "\n";
        module += indent(2) + "input clk: Clock" + "\n";
        module += indent(2) + "input reset: UInt<1>" + "\n";
        for (size_t i = 0; i < sr->narguments(); ++i) {
            module +=
                    indent(2) + "input i" + jive::detail::strfmt(i) + ": {flip ready: UInt<1>, valid: UInt<1>, data: " +
                    to_firrtl_type(&sr->argument(i)->type()) + "}" + "\n";
        }
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(2) + "output o" + jive::detail::strfmt(i) +
                      ": {flip ready: UInt<1>, valid: UInt<1>, data: " +
                      to_firrtl_type(&sr->result(i)->type()) + "}" + "\n";
        }
        module += indent(2) + "; instances" + "\n";
        std::unordered_map<jive::output *, std::string> out_port_map;
        std::unordered_set<jive::output *> used_outputs;
        std::unordered_map<std::string, std::vector<std::string>> handshake_map;
        std::unordered_map<const jive::node*, std::string> node_inst_map;
        for (int i = 0; i < sr->narguments(); ++i) {
            out_port_map[sr->argument(i)] = "i" + jive::detail::strfmt(i);
        }
        // create node modules and ios first
        for (const auto &node : sr->nodes) {
            auto node_module = node_to_string(&node, 2);
            std::string inst_name = node_module + "_inst";
            module += indent(2) + "inst " + inst_name + " of " + node_module + "\n";
            for (int i = 0; i < node.noutputs(); ++i) {
                out_port_map[node.output(i)] = inst_name + ".o" + jive::detail::strfmt(i);
            }
            node_inst_map[&node] = inst_name;
        }
        for (const auto &node : sr->nodes) {
            auto inst_name = node_inst_map[&node];
            module += indent(2) + inst_name + ".clk <= clk\n";
            module += indent(2) + inst_name + ".reset <= reset\n";
            for (int i = 0; i < node.ninputs(); ++i) {
                auto in_name = inst_name + ".i" + jive::detail::strfmt(i);
                assert(out_port_map.count(node.input(i)->origin()));
                auto origin = out_port_map[node.input(i)->origin()];
                used_outputs.insert(node.input(i)->origin());
                module += indent(2) + in_name + ".data <= " + origin + ".data\n";
                module += indent(2) + in_name + ".valid <= " + origin + ".valid\n";
                auto f = handshake_map.find(origin);
                if (f == handshake_map.end()) {
                    handshake_map[origin] = std::vector<std::string>(1, in_name);
                } else {
                    f->second.push_back(in_name);
                }
            }
        }
        if(register_comb){
            // TODO: this should really be done at IR level...
            // insert dummy nodes that ensures there are no comb paths
            for (int i = 0; i < sr->nresults(); ++i) {
                if (dynamic_cast<jive::argument *>(sr->result(i)->origin())) {
                    auto node_module = create_dummy_module(&sr->result(i)->type());
                    auto inst_name = node_module + "_inst";
                    module += indent(2) + "inst " + inst_name + " of " + node_module + "\n";
                    module += indent(2) + inst_name + ".clk <= clk\n";
                    module += indent(2) + inst_name + ".reset <= reset\n";
                    auto origin = out_port_map[sr->result(i)->origin()];
                    auto in_name = inst_name + ".i0";
                    auto out_name = inst_name + ".o0";
                    module += indent(2) + in_name + ".data <= " + origin + ".data\n";
                    module += indent(2) + in_name + ".valid <= " + origin + ".valid\n";
                    auto f = handshake_map.find(origin);
                    if (f == handshake_map.end()) {
                        handshake_map[origin] = std::vector<std::string>(1, in_name);
                    } else {
                        f->second.push_back(in_name);
                    }
                    // overwrite origin string mapping to dummy output - only output comes after this
                    out_port_map[sr->result(i)->origin()] = out_name;
                }
            }
        }
        for (int i = 0; i < sr->nresults(); ++i) {
            auto origin = out_port_map[sr->result(i)->origin()];
            used_outputs.insert(sr->result(i)->origin());
            auto result = "o" + jive::detail::strfmt(i);
            auto f = handshake_map.find(origin);
            if (f == handshake_map.end()) {
                handshake_map[origin] = std::vector<std::string>(1, result);
            } else {
                f->second.push_back(result);
            }
            module += indent(2) + result + ".data <= " + origin + ".data\n";
            module += indent(2) + result + ".valid <= " + origin + ".valid\n";
        }
        for (auto pair: handshake_map) {
            auto origin = pair.first;
            std::string users_ready = "UInt<1>(1)";
            for (auto user: pair.second) {
                users_ready = "and(" + users_ready + ", " + user + ".ready)";
            }
            module += indent(2) + origin + ".ready <= " + users_ready + "\n";
        }
        for (auto pair: out_port_map) {
            auto origin = pair.first;
            if(used_outputs.count(origin)==0){
                module += indent(2) + pair.second+ ".ready <= UInt<1>(1) ; unused\n";
            }
        }

        modules[module_index] = module;
        return module_name;
    }

    std::string
    lambda_node_to_string(const jlm::lambda::node *ln) {
        auto module_name = ln->name() + "_lambda_mod";
        auto module_index = modules.size();
        modules.emplace_back(); // preserve spot
        auto sr = ln->subregion();
        std::string module = indent(1) + "module " + module_name + ":\n";
        // io
        module += indent(2) + "; io" + "\n";
        module += indent(2) + "input clk: Clock" + "\n";
        module += indent(2) + "input reset: UInt<1>" + "\n";
        module += indent(2) + "input i: {flip ready: UInt<1>, valid: UInt<1>";
        for (size_t i = 0; i < sr->narguments(); ++i) {
            module += ", data" + jive::detail::strfmt(i) + ": " +
                      to_firrtl_type(&sr->argument(i)->type());
        }
        module += "}\n";
        module += indent(2) + "output o: {flip ready: UInt<1>, valid: UInt<1>";
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += ", data" + jive::detail::strfmt(i) + ": " +
                      to_firrtl_type(&sr->result(i)->type());
        }
        module += "}\n";
        // registers
        module += indent(2) + "; registers" + "\n";
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(2) + "reg o" + jive::detail::strfmt(i) +
                      "_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))" + "\n";
            module += indent(2) + "reg o" + jive::detail::strfmt(i) + "_data_reg: " +
                      to_firrtl_type(&sr->result(i)->type()) + ", clk" + "\n";
        }
        module += indent(2) + "; instances" + "\n";
        auto sr_module_name = subregion_to_string(sr);
        module += indent(2) + "inst sr of " + sr_module_name + "\n";
        module += indent(2) + "sr.clk <= clk\n";
        module += indent(2) + "sr.reset <= reset\n";
        for (size_t i = 0; i < sr->narguments(); ++i) {
            module += indent(2) + "sr.i" + jive::detail::strfmt(i) + ".valid <= i.valid\n";
            module += indent(2) + "sr.i" + jive::detail::strfmt(i) + ".data <= i.data" + jive::detail::strfmt(i) + "\n";
        }
        // logic
        module += indent(2) + "; logic" + "\n";
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(2) + "sr.o" + jive::detail::strfmt(i) + ".ready <= not(o" + jive::detail::strfmt(i) +
                      "_valid_reg)" + "\n";
        }
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(2) + "when sr.o" + jive::detail::strfmt(i) + ".valid:" + "\n";
            module += indent(3) + "o" + jive::detail::strfmt(i) + "_valid_reg <= UInt<1>(1)" + "\n";
            module += indent(3) + "o" + jive::detail::strfmt(i) + "_data_reg <= sr.o" + jive::detail::strfmt(i) +
                      ".data" + "\n";
        }
        std::string outputs_valids = "UInt<1>(1)"; // True by default
        for (size_t i = 0; i < sr->nresults(); ++i) {
            outputs_valids = "and(" + outputs_valids + ", o" + jive::detail::strfmt(i) + "_valid_reg)";
        }
        module += indent(2) + "node outputs_valid = " + outputs_valids + "\n";
        module += indent(2) + "o.valid <= outputs_valid\n";
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(2) + "o.data" + jive::detail::strfmt(i) + " <= sr.o" + jive::detail::strfmt(i) +
                      ".data" + "\n";
        }
        module += indent(2) + "when and(o.valid, o.ready):\n";
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(3) + "o" + jive::detail::strfmt(i) + "_valid_reg <= UInt<1>(0)" + "\n";
        }
        std::string inputs_ready = "UInt<1>(1)"; // True by default
        for (size_t i = 0; i < sr->nresults(); ++i) {
            inputs_ready = "and(" + inputs_ready + ", sr.i" + jive::detail::strfmt(i) + ".ready)";
        }
        module += indent(2) + "node inputs_ready = " + inputs_ready + "\n";
        module += indent(2) + "i.ready <= inputs_ready\n";
        modules[module_index] = module;
        return module_name;
    }
};

int
main(int argc, char **argv) {
    cmdflags flags;
    parse_cmdflags(argc, argv, flags);

    llvm::LLVMContext ctx;
    llvm::SMDiagnostic err;
    auto lm = llvm::parseIRFile(flags.file, err, ctx);
    if (!lm) {
        err.print(argv[0], llvm::errs());
        exit(1);
    }

    /* LLVM to JLM pass */
    auto jm = jlm::convert_module(*lm);
//    jlm::print(*jm, stdout);
    auto rm = jlm::construct_rvsdg(*jm, flags.sd);

    jlm::dne dne;
    jlm::stats_descriptor sd;
    dne.run(*rm, sd);
    jive::view(rm->graph()->root(), stdout);
    SimpleHLS hls;
    hls.run(*rm, lm.get());
    return 0;
}
