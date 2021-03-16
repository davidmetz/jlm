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

#include <getopt.h>
#include <jlm/opt/dne.hpp>
#include <jive/rvsdg/control.hpp>
#include <jive/types/bitstring.hpp>
#include "../tests/test-operation.hpp"
#include "../tests/test-types.hpp"

class cmdflags {
public:
    inline
    cmdflags()
    {}

    std::string file;
    jlm::stats_descriptor sd;
    std::string hls_function;
};

static void
print_usage(const std::string & app)
{
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
parse_cmdflags(int argc, char ** argv, cmdflags & flags)
{
    static constexpr size_t file = 1;

    static struct option options[] = {
              {"file", required_argument, NULL, file}
            , {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long_only(argc, argv, "", options, NULL)) != -1) {
        switch (opt) {
            case file: { flags.file = optarg; break; }

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
is_identity_mapping(const jive::match_op & op)
{
    for (const auto & pair : op) {
        if (pair.first != pair.second)
            return false;
    }

    return true;
}

class SimpleHLS {
public:
    SimpleHLS(){
    }
    void run(jlm::rvsdg_module & rm){
        auto region = rm.graph()->root();
        for (const auto & node : region->nodes) {
            if(auto ln = dynamic_cast<const jlm::lambda::node*>(&node)){
                std::cout << hls_lambda(ln);
            } else{
                std::cerr << "unknown node " << node.operation().debug_string() << "\n";
            }
        }
    }

private:
    std::unordered_map<jive::output*, std::string> port_map;
    std::unordered_set<jive::output*> port_declared;
    std::string
    create_port_name(jive::output * port)
    {
        std::string name = dynamic_cast<const jive::argument*>(port) ? "a" : "o";
        name += jive::detail::strfmt(port_map.size());
        port_map[port] = name;
        return name;
    }
    std::string
    get_port_name(jive::output * port)
    {
        auto tmp = port_map.find(port);
        std::string name;
        if(tmp!=port_map.end()){
            name = tmp->second;
        } else{
            name = create_port_name(port);
        }
        return name;
    }

    std::string
    to_firrtl_type(const jive::type * type){
        if(auto t = dynamic_cast<const jive::bittype*>(type)){
            // TODO: somehow handle SInt
            return "UInt<"+jive::detail::strfmt(t->nbits())+">";
        } else if(auto t = dynamic_cast<const jive::statetype*>(type)){
            return "UInt<1>";
        }
    }

    std::string
    node_to_string(const jive::node * node){
        std::string result;
        if(auto n = dynamic_cast<const jive::simple_node*>(node)){
//            return "simple node "+node->operation().debug_string()+"\n";
            auto out = n->output(0);
            if(port_declared.find(out)==port_declared.end()){
                result += "node "+get_port_name(out)+"=";
                port_declared.insert(out);
            } else{
                result += get_port_name(out)+"<=";
            }
            if(auto o = dynamic_cast<const jive::match_op *>(&(n->operation()))){
                auto in0 = n->input(0)->origin();
                if(is_identity_mapping(*o)){
                    result += get_port_name(in0);
                } else{
                    throw std::logic_error("non-identity mapping not implemented!");
                }
            } else if (auto o = dynamic_cast<const jive::bitsgt_op *>(&(n->operation()))){
                auto in0 = n->input(0)->origin();
                auto in1 = n->input(1)->origin();
                // TODO: handle sign stuff - maybe tmp nodes with cast?
                result += "gt("+get_port_name(in0)+", "+ get_port_name(in1)+")";
            }else if (auto o = dynamic_cast<const jive::biteq_op *>(&(n->operation()))){
                auto in0 = n->input(0)->origin();
                auto in1 = n->input(1)->origin();
                result += "eq("+get_port_name(in0)+", "+ get_port_name(in1)+")";
            }else if (auto o = dynamic_cast<const jive::bitadd_op *>(&(n->operation()))){
                auto in0 = n->input(0)->origin();
                auto in1 = n->input(1)->origin();
                result += "add("+get_port_name(in0)+", "+ get_port_name(in1)+")";
            }else if (auto o = dynamic_cast<const jive::bitconstant_op *>(&(n->operation()))){
                auto value = o->value();
                result += "UInt<"+jive::detail::strfmt(value.nbits())+">("+value.str()+")";
            } else{
                throw std::logic_error(n->operation().debug_string()+"not implemented!");
            }
            result+=" ; "+n->operation().debug_string()+"\n";
        } else if(auto n = dynamic_cast<const jive::gamma_node*>(node)){
            for (int i = 0; i < n->noutputs(); ++i) {
                auto out = n->output(i);
                if(port_declared.find(out)==port_declared.end()) {
                    result += "wire " + get_port_name(out) + ": " + to_firrtl_type(&out->type())+"\n";
                    port_declared.insert(out);
                }
            }
            for (int i = 0; i < n->nsubregions(); ++i) {
                auto sr = n->subregion(i);
                if(i != 0){
                    result += "else ";
                }
                // TODO: width
                result += "when eq("+get_port_name(n->predicate()->origin())+", UInt<32>("+jive::detail::strfmt(i)+"):\n";
                for (int j = 0; j < sr->narguments(); ++j) {
                    result += "node "+get_port_name(sr->argument(j))+" = "+get_port_name(sr->argument(j)->input()->origin())+"\n";
                }
                for(const auto & node : sr->nodes){
                    result+=node_to_string(&node);
                }
                for (int j = 0; j < sr->nresults(); ++j) {
                    result += get_port_name(sr->result(j)->output())+" <= "+get_port_name(sr->result(j)->origin())+"\n";
                }

            }

        }else{
            throw std::logic_error(n->operation().debug_string()+"not implemented!");
        }
        return result;
    }

    std::string
    hls_lambda(const jlm::lambda::node * ln){
        std::string result;
        auto sr = ln->subregion();
        for (size_t i = 0; i < sr->narguments(); ++i) {
            auto a = sr->argument(i);
            result += "input "+get_port_name(a)+": "+to_firrtl_type(&a->type())+"\n";
            port_declared.insert(a);
        }
        for (size_t i = 0; i < sr->nresults(); ++i) {
            auto o = sr->result(i);
            result += "output "+get_port_name(o->origin())+": "+to_firrtl_type(&o->type())+"\n";
            port_declared.insert(o->origin());
        }
        for(const auto & node : sr->nodes){
            result += node_to_string(&node);
        }
        return result;
    }
};

int
main (int argc, char ** argv)
{
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
    hls.run(*rm);
    return 0;
}
