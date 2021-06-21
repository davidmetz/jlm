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
#include <jive/arch/load.hpp>
#include <jive/arch/store.hpp>
#include <jive/rvsdg/traverser.hpp>
#include <set>
#include <jlm/opt/inversion.hpp>
#include <jlm/opt/cne.hpp>
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

bool
isForbiddenChar(char c) {
    if (('A' <= c && c <= 'Z') ||
        ('a' <= c && c <= 'z') ||
        ('0' <= c && c <= '9') ||
        '_' == c) {
        return false;
    }
    return true;
}

namespace hls {
    class branch_op final : public jive::simple_op {
    public:
        virtual
        ~branch_op() {}

        branch_op(size_t nalternatives, const jive::type &type)
                : jive::simple_op({jive::ctltype(nalternatives), type}, std::vector<jive::port>(nalternatives, type)) {}

        bool
        operator==(const jive::operation &other) const noexcept override {
            auto ot = dynamic_cast<const branch_op *>(&other);
            // check predicate and value
            return ot
                   && ot->argument(0).type() == argument(0).type()
                   && ot->result(0).type() == result(0).type();
        }

        std::string
        debug_string() const override {
            return "HLS_BRANCH";
        }

        std::unique_ptr<jive::operation>
        copy() const override {
            return std::unique_ptr<jive::operation>(new branch_op(*this));
        }

        static std::vector<jive::output *>
        create(
                jive::output &predicate,
                jive::output &value
        ) {
            auto ctl = dynamic_cast<const jive::ctltype *>(&predicate.type());
            if (!ctl)
                throw jlm::error("Predicate needs to be a ctltype.");

            auto region = predicate.region();
            branch_op op(ctl->nalternatives(), value.type());
            return jive::simple_node::create_normalized(region, op, {&predicate, &value});
        }
    };

    class fork_op final : public jive::simple_op {
    public:
        virtual
        ~fork_op() {}

        fork_op(size_t nalternatives, const jive::type &type)
                : jive::simple_op({type}, std::vector<jive::port>(nalternatives, type)) {}

        bool
        operator==(const jive::operation &other) const noexcept override {
            auto ot = dynamic_cast<const branch_op *>(&other);
            // check predicate and value
            return ot
                   && ot->argument(0).type() == argument(0).type()
                   && ot->narguments() == narguments();
        }

        std::string
        debug_string() const override {
            return "HLS_FORK";
        }

        std::unique_ptr<jive::operation>
        copy() const override {
            return std::unique_ptr<jive::operation>(new fork_op(*this));
        }

        static std::vector<jive::output *>
        create(
                size_t nalternatives,
                jive::output &value
        ) {

            auto region = value.region();
            fork_op op(nalternatives, value.type());
            return jive::simple_node::create_normalized(region, op, {&value});
        }
    };

    class merge_op final : public jive::simple_op {
    public:
        virtual
        ~merge_op() {}

        merge_op(size_t nalternatives, const jive::type &type)
                : jive::simple_op(std::vector<jive::port>(nalternatives, type), {type}) {}

        bool
        operator==(const jive::operation &other) const noexcept override {
            auto ot = dynamic_cast<const merge_op *>(&other);
            return ot
                   && ot->narguments() == narguments()
                   && ot->argument(0).type() == argument(0).type();
        }

        std::string
        debug_string() const override {
            return "HLS_MERGE";
        }

        std::unique_ptr<jive::operation>
        copy() const override {
            return std::unique_ptr<jive::operation>(new merge_op(*this));
        }

        static std::vector<jive::output *>
        create(
                const std::vector<jive::output *> &alternatives
        ) {
            if (alternatives.empty())
                throw jlm::error("Insufficient number of operands.");


            auto region = alternatives.front()->region();
            merge_op op(alternatives.size(), alternatives.front()->type());
            return jive::simple_node::create_normalized(region, op, alternatives);
        }
    };

    class mux_op final : public jive::simple_op {
    public:
        virtual
        ~mux_op() {}

        mux_op(size_t nalternatives, const jive::type &type, bool discarding)
                : jive::simple_op(create_portvector(nalternatives, type), {type}), discarding(discarding) {}

        bool
        operator==(const jive::operation &other) const noexcept override {
            auto ot = dynamic_cast<const mux_op *>(&other);
            // check predicate and value
            return ot
                   && ot->argument(0).type() == argument(0).type()
                   && ot->result(0).type() == result(0).type()
                   && ot->discarding == discarding;
        }

        std::string
        debug_string() const override {
            return discarding ? "HLS_DMUX" : "HLS_NDMUX";
        }

        std::unique_ptr<jive::operation>
        copy() const override {
            return std::unique_ptr<jive::operation>(new mux_op(*this));
        }

        static std::vector<jive::output *>
        create(
                jive::output &predicate,
                const std::vector<jive::output *> &alternatives,
                bool discarding
        ) {
            if (alternatives.empty())
                throw jlm::error("Insufficient number of operands.");
            auto ctl = dynamic_cast<const jive::ctltype *>(&predicate.type());
            if (!ctl)
                throw jlm::error("Predicate needs to be a ctltype.");
            if (alternatives.size() != ctl->nalternatives())
                throw jlm::error("Alternatives and predicate do not match.");


            auto region = predicate.region();
            auto operands = std::vector<jive::output *>();
            operands.push_back(&predicate);
            operands.insert(operands.end(), alternatives.begin(), alternatives.end());
            mux_op op(alternatives.size(), alternatives.front()->type(), discarding);
            return jive::simple_node::create_normalized(region, op, operands);
        }


        bool discarding;
    private:

        static std::vector<jive::port>
        create_portvector(size_t nalternatives, const jive::type &type) {
            auto vec = std::vector<jive::port>(nalternatives + 1, type);
            vec[0] = jive::ctltype(nalternatives);
            return vec;
        }
    };

    class sink_op final : public jive::simple_op {
    public:
        virtual
        ~sink_op() {}

        sink_op(const jive::type &type)
                : jive::simple_op({type}, {}) {}

        bool
        operator==(const jive::operation &other) const noexcept override {
            auto ot = dynamic_cast<const sink_op *>(&other);
            return ot && ot->argument(0).type() == argument(0).type();
        }

        std::string
        debug_string() const override {
            return "HLS_SINK";
        }

        std::unique_ptr<jive::operation>
        copy() const override {
            return std::unique_ptr<jive::operation>(new sink_op(*this));
        }

        static std::vector<jive::output *>
        create(
                jive::output &value
        ) {
            auto region = value.region();
            sink_op op(value.type());
            return jive::simple_node::create_normalized(region, op, {&value});
        }
    };

    class dummy_op final : public jive::simple_op {
    public:
        virtual
        ~dummy_op() {}

        dummy_op(const jive::type &type)
                : jive::simple_op({}, {type}) {}

        bool
        operator==(const jive::operation &other) const noexcept override {
//            auto ot = dynamic_cast<const dummy_op *>(&other);
//            return ot && ot->result(0).type() == result(0).type();
            // dummy nodes should all be distinct and not be reused
            return false;
        }

        std::string
        debug_string() const override {
            return "HLS_DUMMY";
        }

        std::unique_ptr<jive::operation>
        copy() const override {
            return std::unique_ptr<jive::operation>(new dummy_op(*this));
        }

        static std::vector<jive::output *>
        create(
                jive::output &value,
                jive::region *region
        ) {
            dummy_op op(value.type());
            return jive::simple_node::create_normalized(region, op, {});
        }
    };

    class predicate_buffer_op final : public jive::simple_op {
    public:
        virtual
        ~predicate_buffer_op() {}

        predicate_buffer_op(const jive::ctltype &type)
                : jive::simple_op({type}, {type}) {}

        bool
        operator==(const jive::operation &other) const noexcept override {
            auto ot = dynamic_cast<const predicate_buffer_op *>(&other);
            return ot && ot->result(0).type() == result(0).type();
        }

        std::string
        debug_string() const override {
            return "HLS_PRED_BUF";
        }

        std::unique_ptr<jive::operation>
        copy() const override {
            return std::unique_ptr<jive::operation>(new predicate_buffer_op(*this));
        }

        static std::vector<jive::output *>
        create(
                jive::output &predicate
        ) {
            auto region = predicate.region();
            auto ctl = dynamic_cast<const jive::ctltype *>(&predicate.type());
            if (!ctl)
                throw jlm::error("Predicate needs to be a ctltype.");
            predicate_buffer_op op(*ctl);
            return jive::simple_node::create_normalized(region, op, {&predicate});
        }
    };

    class buffer_op final : public jive::simple_op {
    public:
        virtual
        ~buffer_op() {}

        buffer_op(const jive::type &type, size_t capacity)
                : jive::simple_op({type}, {type}), capacity(capacity) {}

        bool
        operator==(const jive::operation &other) const noexcept override {
            auto ot = dynamic_cast<const buffer_op *>(&other);
            return ot && ot->result(0).type() == result(0).type();
        }

        std::string
        debug_string() const override {
            return "HLS_BUF";
        }

        std::unique_ptr<jive::operation>
        copy() const override {
            return std::unique_ptr<jive::operation>(new buffer_op(*this));
        }

        static std::vector<jive::output *>
        create(
                jive::output &value,
                size_t capacity
        ) {
            auto region = value.region();
            buffer_op op(value.type(), capacity);
            return jive::simple_node::create_normalized(region, op, {&value});
        }

        size_t capacity;
    private:
    };

    class triggertype final : public jive::statetype {
    public:
        virtual
        ~triggertype() {}

        triggertype() : jive::statetype() {}

        std::string
        debug_string() const override {
            return "trigger";
        };

        bool
        operator==(const jive::type &other) const noexcept override {
            auto type = dynamic_cast<const triggertype *>(&other);
            return type;
        };

        virtual std::unique_ptr<jive::type>
        copy() const override {
            return std::unique_ptr<jive::type>(new triggertype(*this));
        }

    private:
    };

    const triggertype trigger;


    class trigger_op final : public jive::simple_op {
    public:
        virtual
        ~trigger_op() {}

        trigger_op(const jive::type &type)
                : jive::simple_op({trigger, type}, {type}) {}

        bool
        operator==(const jive::operation &other) const noexcept override {
            auto ot = dynamic_cast<const branch_op *>(&other);
            // check predicate and value
            return ot
                   && ot->argument(0).type() == argument(0).type()
                   && ot->result(0).type() == result(0).type();
        }

        std::string
        debug_string() const override {
            return "HLS_TRIGGER";
        }

        std::unique_ptr<jive::operation>
        copy() const override {
            return std::unique_ptr<jive::operation>(new trigger_op(*this));
        }

        static std::vector<jive::output *>
        create(
                jive::output &tg,
                jive::output &value
        ) {
            if (tg.type() != trigger)
                throw jlm::error("Trigger needs to be a triggertype.");

            auto region = value.region();
            trigger_op op(value.type());
            return jive::simple_node::create_normalized(region, op, {&tg, &value});
        }
    };
}

namespace hls {
    class loop_op final : public jive::structural_op {
    public:
        virtual
        ~loop_op() noexcept {}

        std::string
        debug_string() const override {
            return "HLS_LOOP";
        }

        std::unique_ptr<jive::operation>
        copy() const override {
            return std::unique_ptr<jive::operation>(new loop_op(*this));
        }
    };


    class loop_node final : public jive::structural_node {
    public:

        virtual
        ~loop_node() {}

    private:
        inline
        loop_node(jive::region *parent)
                : structural_node(loop_op(), parent, 1) {
        }

        jive::output *_predicate_buffer;

    public:
        static loop_node *
        create(jive::region *parent, bool init = true) {
            auto ln = new loop_node(parent);
            if (init) {
                auto predicate = jive_control_false(ln->subregion());
                jive::result::create(ln->subregion(), predicate, nullptr, jive::ctltype(2));
                auto pred_arg = jive::argument::create(ln->subregion(), nullptr, jive::ctltype(2));
                ln->_predicate_buffer = hls::predicate_buffer_op::create(*pred_arg)[0];
            }
            return ln;
        }

        inline jive::region *
        subregion() const noexcept {
            return structural_node::subregion(0);
        }

        inline jive::result *
        predicate() const noexcept {
            auto result = subregion()->result(0);
            JIVE_DEBUG_ASSERT(dynamic_cast<const jive::ctltype *>(&result->type()));
            return result;
        }

        inline jive::output *
        predicate_buffer() const noexcept {
            return _predicate_buffer;
        }

        inline void
        set_predicate(jive::output *p) {
            auto node = jive::node_output::node(predicate()->origin());
            predicate()->origin()->divert_users(p);
            if (node && !node->has_users())
                remove(node);
        }

        inline size_t
        nloopvars() const noexcept {
            JIVE_DEBUG_ASSERT(ninputs() == noutputs());
            return ninputs();
        }
        // TODO: implement loop_node begin() and end()
//        inline loop_node::loopvar_iterator
//        begin() const
//        {
//            if (ninputs() == 0)
//                return loopvar_iterator(nullptr);
//
//            return loopvar_iterator(output(0));
//        }
//
//        inline loop_node::loopvar_iterator
//        end() const
//        {
//            return loopvar_iterator(nullptr);
//        }

        // TODO: implement if using custom inputs/outputs
//        jive::node_input *
//        input(size_t index) const noexcept;
//
//        jive::node_output *
//        output(size_t index) const noexcept;

        jive::structural_output *
        add_loopvar(jive::output *origin, jive::output **buffer = nullptr) {
            jive::structural_input::create(this, origin, origin->type());
            jive::structural_output::create(this, origin->type());

            auto input = loop_node::input(ninputs() - 1);
            auto output = loop_node::output(noutputs() - 1);

            auto argument_in = jive::argument::create(subregion(), input, origin->type());
            auto argument_loop = jive::argument::create(subregion(), nullptr, origin->type());

            auto mux = hls::mux_op::create(*predicate_buffer(), {argument_in, argument_loop}, false)[0];
            auto buf = hls::buffer_op::create(*mux, 2)[0];
            auto branch = hls::branch_op::create(*predicate()->origin(), *buf);
            if (buffer != nullptr) {
                *buffer = buf;
            }

            auto result_out = jive::result::create(subregion(), branch[0], output, origin->type());
            auto result_loop = jive::result::create(subregion(), branch[1], nullptr, origin->type());
            return output;
        }

        virtual loop_node *
        copy(jive::region *region, jive::substitution_map &smap) const override {
            // TODO: fix this - broken
            auto nf = graph()->node_normal_form(typeid(jive::operation));
            nf->set_mutable(false);

            jive::substitution_map rmap;
            auto loop = create(region, false);

            // add nullptr to rmap so we can copy loop arguments and results
            rmap.insert((jive::output *) nullptr, nullptr);
            rmap.insert((jive::structural_input *) nullptr, nullptr);
            // create in and outputs and add them to rmap
            for (int i = 0; i < nloopvars(); ++i) {
                auto in_origin = smap.lookup(input(i)->origin());
                loop->add_input(
                        std::unique_ptr<jive::node_input>(new jive::node_input(in_origin, loop, in_origin->type())));
                rmap.insert(input(i), loop->input(i));
                loop->add_output(std::unique_ptr<jive::node_output>(new jive::node_output(loop, in_origin->type())));
                rmap.insert(output(i), loop->output(i));
            }

            subregion()->copy(loop->subregion(), rmap, true, true);
            loop->_predicate_buffer = rmap.lookup(_predicate_buffer);

            nf->set_mutable(true);
            return loop;
        }
    };
}

namespace hls {
    namespace passes {
        static void hls_gamma_conv_nonspec(jive::gamma_node *gamma) {
            jive::substitution_map smap;
            // create a branch for each entryvar and map the corresponding argument of each subregion to an output of the branch
            auto pro = gamma->predicate()->origin();
            for (size_t i = 0; i < gamma->nentryvars(); i++) {
                auto envo = gamma->entryvar(i)->origin();
                auto bros = hls::branch_op::create(*pro, *envo);
                for (size_t s = 0; s < gamma->nsubregions(); s++) {
                    smap.insert(gamma->subregion(s)->argument(i), bros[s]);
                }
            }
            // copy each of the subregions
            for (size_t s = 0; s < gamma->nsubregions(); s++) {
//                std::cout << "copying gamma subregion:\n";
//                jive::view(gamma->subregion(s), stdout);
                gamma->subregion(s)->copy(gamma->region(), smap, false, false);
            }
            for (size_t i = 0; i < gamma->nexitvars(); i++) {
                std::vector<jive::output *> alternatives;
                for (size_t s = 0; s < gamma->nsubregions(); s++) {
                    alternatives.push_back(smap.lookup(gamma->subregion(s)->result(i)->origin()));
                }
                // create merge nodes for each exitvar
                auto merge = hls::merge_op::create(alternatives);
                // divert users of exitvars to merge instead
                gamma->exitvar(i)->divert_users(merge[0]);
            }
            remove(gamma);
        }

        static void
        hls_gamma_conv(jive::region *region) {
            for (auto &node : jive::topdown_traverser(region)) {
                if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
                    for (size_t n = 0; n < structnode->nsubregions(); n++)
                        hls_gamma_conv(structnode->subregion(n));
                    auto gamma = dynamic_cast<jive::gamma_node *>(node);
                    if (gamma) {
                        // TODO: add speculative branch
                        hls_gamma_conv_nonspec(gamma);
                    }
                }
            }
        }

        static void
        hls_gamma_conv(jlm::rvsdg_module &rm) {
            auto &graph = *rm.graph();
            auto root = graph.root();
            hls_gamma_conv(root);
        }

        static void hls_theta_conv(jive::theta_node *theta) {
            jive::substitution_map smap;

            auto loop = hls::loop_node::create(theta->region());
            std::vector<jive::input *> branches;

            // add loopvars and populate the smap
            for (size_t i = 0; i < theta->ninputs(); i++) {
                jive::output *buffer;
                loop->add_loopvar(theta->input(i)->origin(), &buffer);
                smap.insert(theta->input(i)->argument(), buffer);
                // buffer out is only used by branch
                branches.push_back(*buffer->begin());
                // divert theta outputs
                theta->output(i)->divert_users(loop->output(i));
            }

            std::cout << "copying theta region:\n";
            jive::view(theta->subregion(), stdout);

            // copy contents of theta
            theta->subregion()->copy(loop->subregion(), smap, false, false);

            // connect predicate/branches in loop to results
            loop->set_predicate(smap.lookup(theta->predicate()->origin()));
            for (size_t i = 0; i < theta->ninputs(); i++) {
                branches[i]->divert_to(smap.lookup(theta->input(i)->result()->origin()));
            }
            remove(theta);
        }


        static void
        hls_theta_conv(jive::region *region) {
            for (auto &node : jive::topdown_traverser(region)) {
                if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
                    auto theta = dynamic_cast<jive::theta_node *>(node);
                    if (theta) {
                        hls_theta_conv(theta);
                        hls_theta_conv(region);
                    } else {
                        for (size_t n = 0; n < structnode->nsubregions(); n++)
                            hls_theta_conv(structnode->subregion(n));
                    }
                }
            }
        }

        static void
        hls_theta_conv(jlm::rvsdg_module &rm) {
            auto &graph = *rm.graph();
            auto root = graph.root();
            hls_theta_conv(root);
        }


        static void
        hls_add_sinks(jive::region *region) {
            for (int i = 0; i < region->narguments(); ++i) {
                auto arg = region->argument(i);
                if (!arg->nusers()) {
                    hls::sink_op::create(*arg);
                }
            }
            for (auto &node : jive::topdown_traverser(region)) {
                if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
                    for (size_t n = 0; n < structnode->nsubregions(); n++) {
                        hls_add_sinks(structnode->subregion(n));
                    }
                }

                for (int i = 0; i < node->noutputs(); ++i) {
                    auto out = node->output(i);
                    if (!out->nusers()) {
                        hls::sink_op::create(*out);
                    }
                }
            }
        }

        static void
        hls_add_sinks(jlm::rvsdg_module &rm) {
            auto &graph = *rm.graph();
            auto root = graph.root();
            hls_add_sinks(root);
        }

        static void
        hls_add_forks(jive::region *region) {
            for (size_t i = 0; i < region->narguments(); ++i) {
                auto arg = region->argument(i);
                if (arg->nusers() > 1) {
                    std::vector<jive::input *> users;
                    users.insert(users.begin(), arg->begin(), arg->end());
                    auto fork = hls::fork_op::create(arg->nusers(), *arg);
                    for (size_t j = 0; j < users.size(); j++) {
                        users[j]->divert_to(fork[j]);
                    }
                }
            }
            for (auto &node : jive::topdown_traverser(region)) {
                if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
                    for (size_t n = 0; n < structnode->nsubregions(); n++) {
                        hls_add_forks(structnode->subregion(n));
                    }
                }
                // TODO: simplify if forks depend on a fork
                for (size_t i = 0; i < node->noutputs(); ++i) {
                    auto out = node->output(i);
                    if (out->nusers() > 1) {
                        std::vector<jive::input *> users;
                        users.insert(users.begin(), out->begin(), out->end());
                        auto fork = hls::fork_op::create(out->nusers(), *out);
                        for (size_t j = 0; j < users.size(); j++) {
                            users[j]->divert_to(fork[j]);
                        }
                    }
                }
            }
        }

        static void
        hls_add_forks(jlm::rvsdg_module &rm) {
            auto &graph = *rm.graph();
            auto root = graph.root();
            hls_add_forks(root);
        }

        static bool is_passthrough(const jive::argument *arg) {
            if (arg->nusers() == 1) {
                auto res = dynamic_cast<jive::result *>(*arg->begin());
                // used only by a result
                if (res) {
                    return true;
                }
            }
            return false;
        }

        static bool is_passthrough(const jive::result *res) {
            auto arg = dynamic_cast<jive::argument *>(res->origin());
            if (arg) {
                return true;
            }
            return false;
        }

        static jlm::lambda::node *
        remove_lambda_passthrough(jlm::lambda::node *ln) {
            auto old_fcttype = ln->type();
            // TODO: add support for cvarguments
            assert(ln->ncvarguments() == 0);
            std::vector<const jive::type *> new_argument_types;
            for (int i = 0; i < old_fcttype.narguments(); ++i) {
                auto arg = ln->subregion()->argument(i);
                auto argtype = &old_fcttype.argument_type(i);
                assert(*argtype == arg->type());
                if (!is_passthrough(arg)) {
                    new_argument_types.push_back(argtype);
                }
            }
            std::vector<const jive::type *> new_result_types;
            for (int i = 0; i < old_fcttype.nresults(); ++i) {
                auto res = ln->subregion()->result(i);
                auto restype = &old_fcttype.result_type(i);
                assert(*restype == res->type());
                if (!is_passthrough(res)) {
                    new_result_types.push_back(&old_fcttype.result_type(i));
                }
            }
            jive::fcttype new_fcttype(new_argument_types, new_result_types);
            auto new_lambda = jlm::lambda::node::create(ln->region(), new_fcttype, ln->name(), ln->linkage(),
                                                        ln->attributes());

            jive::substitution_map smap;
            int new_i = 0;
            for (int i = 0; i < ln->subregion()->narguments(); ++i) {
                auto arg = ln->subregion()->argument(i);
                if (!is_passthrough(arg)) {
                    smap.insert(arg, new_lambda->subregion()->argument(new_i));
                    new_i++;
                }
            }
            ln->subregion()->copy(new_lambda->subregion(), smap, false, false);

            std::vector<jive::output *> new_results;
            for (int i = 0; i < ln->subregion()->nresults(); ++i) {
                auto res = ln->subregion()->result(i);
                if (!is_passthrough(res)) {
                    new_results.push_back(smap.lookup(res->origin()));
                }
            }
            auto new_out = new_lambda->finalize(new_results);

            // TODO handle functions at other levels?
            assert(ln->region() == ln->region()->graph()->root());
            assert((*ln->output()->begin())->region() == ln->region()->graph()->root());

//            ln->output()->divert_users(new_out);
            ln->region()->remove_result((*ln->output()->begin())->index());
            remove(ln);
            jive::result::create(new_lambda->region(), new_out, nullptr, new_out->type());
            return new_lambda;
        }

        static void remove_region_passthrough(const jive::argument *arg) {
            auto res = dynamic_cast<jive::result *>(*arg->begin());
            auto origin = arg->input()->origin();
            // divert users of output to origin of input
            arg->region()->node()->output(res->output()->index())->divert_users(origin);
            arg->region()->node()->remove_input(arg->input()->index());
            arg->region()->node()->remove_output(res->output()->index());
            arg->region()->remove_result(res->index());
            arg->region()->remove_argument(arg->index());
        }

        static void remove_gamma_passthrough(jive::gamma_node *gn) {// remove inputs in reverse
            for (int i = gn->nentryvars() - 1; i >= 0; --i) {
                bool can_remove = true;
                for (size_t n = 0; n < gn->nsubregions(); n++) {
                    auto sr = gn->subregion(n);
                    can_remove &= is_passthrough(sr->argument(i));
                }
                if (can_remove) {
                    auto origin = gn->entryvar(i)->origin();
                    // remove input
                    gn->remove_input(i + 1);
                    // divert users of output to origin of input
                    auto res_index = dynamic_cast<jive::result *>(*gn->subregion(0)->argument(
                            i)->begin())->index();
                    gn->output(res_index)->divert_users(origin);
                    gn->remove_output(res_index);
                    for (int j = 0; j < gn->nsubregions(); ++j) {
                        gn->subregion(j)->remove_result(res_index);
                        gn->subregion(j)->remove_argument(i);
                    }
                }
            }
        }

        static void
        hls_remove_unused_state(jive::region *region, bool can_remove_arguments = true) {
            // process children first so that unnecessary users get removed
            for (auto &node : jive::topdown_traverser(region)) {
                if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
                    if (auto gn = dynamic_cast<jive::gamma_node *>(node)) {
                        // process subnodes first
                        for (size_t n = 0; n < gn->nsubregions(); n++) {
                            hls_remove_unused_state(gn->subregion(n), false);
                        }
                        remove_gamma_passthrough(gn);
                    } else if (auto ln = dynamic_cast<jlm::lambda::node *>(node)) {
                        hls_remove_unused_state(structnode->subregion(0), false);
                        remove_lambda_passthrough(ln);
                    } else {
                        assert(structnode->nsubregions() == 1);
                        hls_remove_unused_state(structnode->subregion(0));
                    }
                }
            }
            if (can_remove_arguments) {
                // check if an input is passed through unnecessarily
                for (int i = region->narguments() - 1; i >= 0; --i) {
                    auto arg = region->argument(i);
                    if (is_passthrough(arg)) {
                        remove_region_passthrough(arg);
                    }
                }
            }
        }

        static void
        hls_remove_unused_state(jlm::rvsdg_module &rm) {
            auto &graph = *rm.graph();
            auto root = graph.root();
            hls_remove_unused_state(root);
        }

        static jlm::lambda::node *
        add_lambda_argument(jlm::lambda::node *ln, const jive::type *type) {
            auto old_fcttype = ln->type();
            // TODO: add support for cvarguments
            assert(ln->ncvarguments() == 0);
            std::vector<const jive::type *> new_argument_types;
            for (int i = 0; i < old_fcttype.narguments(); ++i) {
                new_argument_types.push_back(&old_fcttype.argument_type(i));
            }
            new_argument_types.push_back(type);
            std::vector<const jive::type *> new_result_types;
            for (int i = 0; i < old_fcttype.nresults(); ++i) {
                new_result_types.push_back(&old_fcttype.result_type(i));
            }
            jive::fcttype new_fcttype(new_argument_types, new_result_types);
            auto new_lambda = jlm::lambda::node::create(ln->region(), new_fcttype, ln->name(), ln->linkage(),
                                                        ln->attributes());

            jive::substitution_map smap;
            for (int i = 0; i < ln->subregion()->narguments(); ++i) {
                smap.insert(ln->subregion()->argument(i), new_lambda->subregion()->argument(i));
            }
            ln->subregion()->copy(new_lambda->subregion(), smap, false, false);

            std::vector<jive::output *> new_results;
            for (int i = 0; i < ln->subregion()->nresults(); ++i) {
                new_results.push_back(smap.lookup(ln->subregion()->result(i)->origin()));
            }
            auto new_out = new_lambda->finalize(new_results);

            // TODO handle functions at other levels?
            assert(ln->region() == ln->region()->graph()->root());
            assert((*ln->output()->begin())->region() == ln->region()->graph()->root());

//            ln->output()->divert_users(new_out);
            ln->region()->remove_result((*ln->output()->begin())->index());
            remove(ln);
            jive::result::create(new_lambda->region(), new_out, nullptr, new_out->type());
            return new_lambda;
        }

        static jive::output *
        get_trigger(jive::region *region) {
            for (int i = 0; i < region->narguments(); ++i) {
                if (region->argument(i)->type() == hls::trigger) {
                    return region->argument(i);
                }
            }
            return nullptr;
        }

        static void
        hls_add_triggers(jive::region *region) {
            auto trigger = get_trigger(region);
            for (auto &node : jive::topdown_traverser(region)) {
                if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
                    if (auto ln = dynamic_cast<jlm::lambda::node *>(node)) {
                        if (!get_trigger(ln->subregion())) {
                            auto new_lambda = add_lambda_argument(ln, &(hls::trigger));
                            hls_add_triggers(new_lambda->subregion());
                        }
                    } else if (auto t = dynamic_cast<jive::theta_node *>(node)) {
                        assert(trigger != nullptr);
                        assert(get_trigger(t->subregion()) == nullptr);
                        t->add_loopvar(trigger);
                        hls_add_triggers(t->subregion());
                    } else if (auto gn = dynamic_cast<jive::gamma_node *>(node)) {
                        assert(trigger != nullptr);
                        assert(get_trigger(gn->subregion(0)) == nullptr);
                        auto in = gn->add_entryvar(trigger);
                        for (int i = 0; i < gn->nsubregions(); ++i) {
                            hls_add_triggers(gn->subregion(i));
                        }
                    } else {
                        throw jlm::error("Unexpected node type: " + node->operation().debug_string());
                    }
                } else if (auto sn = dynamic_cast<jive::simple_node *>(node)) {
                    assert(trigger != nullptr);
                    if (dynamic_cast<const jive::bitconstant_op *>(&(sn->operation())) ||
                        dynamic_cast<const jlm::undef_constant_op *>(&(sn->operation())) ||
                        dynamic_cast<const jive::ctlconstant_op *>(&(sn->operation()))) {
                        auto orig_out = sn->output(0);
                        std::vector<jive::input *> previous_users(orig_out->begin(), orig_out->end());
                        auto gated = hls::trigger_op::create(*trigger, *orig_out)[0];
                        for (auto user: previous_users) {
                            user->divert_to(gated);
                        }
//                        std::cout << "adding triggers:\n";
//                        jive::view(region, stdout);
                    }
                } else {
                    throw jlm::error("Unexpected node type: " + node->operation().debug_string());
                }
            }
        }

        static void
        hls_add_triggers(jlm::rvsdg_module &rm) {
            auto &graph = *rm.graph();
            auto root = graph.root();
            hls_add_triggers(root);
        }

        static void
        hls_check_graph(jlm::rvsdg_module &rm) {
            auto &graph = *rm.graph();
            auto root = graph.root();
            if (root->nodes.size() != 1) {
                throw jlm::error("Root should have only one node now");
            }
            auto ln = dynamic_cast<const jlm::lambda::node *>(root->nodes.begin().ptr());
            if (!ln) {
                throw jlm::error("Node needs to be a lambda");
            }
            for (auto &node : jive::topdown_traverser(ln->subregion())) {
                if (auto structnode = dynamic_cast<jive::structural_node *>(node)) {
                    throw jlm::error("There should be only simple nodes");
                }
                // TODO: check if constants have triggers
                for (size_t i = 0; i < node->noutputs(); i++) {
                    if (node->output(i)->nusers() == 0) {
                        throw jlm::error("Output has no users");
                    } else if (node->output(i)->nusers() > 1) {
                        throw jlm::error("Output has more than one user");
                    }
                }
            }
        }
    }
}

class FirrtlModule {
public:
    FirrtlModule(const std::string &name, const std::string &firrtl, bool hasMem) : has_mem(hasMem), name(name),
                                                                                    firrtl(firrtl) {}

public:
    bool has_mem;
    std::string name;
    std::string firrtl;
};

class DotHLS {
public:
    void run(jlm::rvsdg_module &rm, llvm::Module *lm) {
        std::string dot;
        auto region = rm.graph()->root();
        if (auto ln = dynamic_cast<const jlm::lambda::node *>(region->nodes.begin().ptr())) {
            dot = subregion_to_dot(ln->subregion());
        } else {
            throw jlm::error("Root should have only one lambda node now");
        }
        auto source_file_name = lm->getSourceFileName();
        std::string file_name = source_file_name.substr(0, source_file_name.find_last_of('.')) + ".dot";
        std::ofstream dot_file;
        dot_file.open(file_name);
        dot_file << dot;
        dot_file.close();
    }

private:
    std::unordered_map<const jive::node *, std::string> node_map;
    std::unordered_map<jive::output *, std::string> output_map;

    std::string
    get_node_name(const jive::node *node) {
        auto found = node_map.find(node);
        if (found != node_map.end()) {
            return found->second;
        }
        auto new_name = jive::detail::strfmt("node_", node->operation().debug_string(), "_", node_map.size());
        // remove chars that are not valid in firrtl module names
        std::replace_if(new_name.begin(), new_name.end(), isForbiddenChar, '_');
        node_map[node] = new_name;
        return new_name;
    }

    std::string
    get_port_name(jive::input *port) {
        std::string result;
        if (auto p = dynamic_cast<const jive::node_input *>(port)) {
            result += "i";
        } else if (auto p = dynamic_cast<const jive::result *>(port)) {
            result += "r";
        } else {
            throw std::logic_error(port->debug_string() + " not implemented!");
        }
        result += jive::detail::strfmt(port->index());
        return result;
    }

    std::string
    get_port_name(jive::output *port) {
        if (port == nullptr) {
            throw std::logic_error("nullptr!");
        }
        std::string result;
        if (auto p = dynamic_cast<const jive::argument *>(port)) {
            result += "a";
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

    std::string argument_to_dot(jive::argument *port) {
        auto name = get_port_name(port);

        auto dot = \
        "{rank=source; " +
        name +
        " [shape=plaintext label=<\n"
        "            <TABLE BORDER=\"0\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"0\">\n"
        "                <TR>\n"
        "                    <TD BORDER=\"1\" CELLPADDING=\"1\"><FONT POINT-SIZE=\"10\">" +
        name +
        "</FONT></TD>\n"
        "                </TR>\n"
        "            </TABLE>\n"
        ">];}\n";
        return dot;
    }

    std::string result_to_dot(jive::result *port) {
        auto name = get_port_name(port);

        auto dot = \
        "{rank=sink; " +
        name +
        " [shape=plaintext label=<\n"
        "            <TABLE BORDER=\"0\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"0\">\n"
        "                <TR>\n"
        "                    <TD BORDER=\"1\" CELLPADDING=\"1\"><FONT POINT-SIZE=\"10\">" +
        name +
        "</FONT></TD>\n"
        "                </TR>\n"
        "            </TABLE>\n"
        ">];}\n";
        return dot;
    }

    std::string node_to_dot(const jive::node *node) {
        // dot inspired by https://stackoverflow.com/questions/42157650/moving-graphviz-edge-out-of-the-way
        auto SPACER = "                    <TD WIDTH=\"10\"></TD>\n";
        auto name = get_node_name(node);
        auto opname = node->operation().debug_string();
        std::replace_if(opname.begin(), opname.end(), isForbiddenChar, '_');

        std::string inputs;
        for (int i = 0; i < node->ninputs(); ++i) {
            if (i != 0) {
                inputs += SPACER;
            }
            auto in = get_port_name(node->input(i));
            inputs += \
            "                    <TD PORT=\"" + in + "\" BORDER=\"1\" CELLPADDING=\"1\"><FONT POINT-SIZE=\"10\">" + in +
            "</FONT></TD>\n";
        }

        std::string outputs;
        for (int i = 0; i < node->noutputs(); ++i) {
            if (i != 0) {
                outputs += SPACER;
            }
            auto out = get_port_name(node->output(i));
            outputs += \
            "                    <TD PORT=\"" + out + "\" BORDER=\"1\" CELLPADDING=\"1\"><FONT POINT-SIZE=\"10\">" +
            out + "</FONT></TD>\n";
        }

        auto dot = \
        name +
        " [shape=plaintext label=<\n"
        "<TABLE BORDER=\"0\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"0\">\n"
        // inputs
        "    <TR>\n"
        "        <TD BORDER=\"0\">\n"
        "            <TABLE BORDER=\"0\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"0\">\n"
        "                <TR>\n"
        "                    <TD WIDTH=\"20\"></TD>\n" +
        inputs +
        "                    <TD WIDTH=\"20\"></TD>\n"
        "                </TR>\n"
        "            </TABLE>\n"
        "        </TD>\n"
        "    </TR>\n"
        "    <TR>\n"
        "        <TD BORDER=\"1\" STYLE=\"ROUNDED\" CELLPADDING=\"4\" COLOR=\"black\">" +
        opname +
        "<BR/><FONT POINT-SIZE=\"10\">" +
        name +
        "</FONT></TD>\n"
        "    </TR>\n"
        "    <TR>\n"
        "        <TD BORDER=\"0\">\n"
        "            <TABLE BORDER=\"0\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"0\">\n"
        "                <TR>\n"
        "                    <TD WIDTH=\"20\"></TD>\n" +
        outputs +
        "                    <TD WIDTH=\"20\"></TD>\n"
        "                </TR>\n"
        "            </TABLE>\n"
        "        </TD>\n"
        "    </TR>\n"
        "</TABLE>\n"
        ">];\n";
        return dot;
    }

    std::string
    edge(std::string src, std::string snk, const jive::type &type, bool back=false) {
        auto color = "black";
        if (dynamic_cast<const jive::ctltype *>(&type)) {
            color = "green";
        }
        if(!back){
            return src + " -> " + snk + " [style=\"\", arrowhead=\"normal\", color=" + color +
                   ", headlabel=<>, fontsize=10, labelangle=45, labeldistance=2.0, labelfontcolor=black];\n";
        }
        return snk + " -> " + src + " [style=\"\", arrowhead=\"normal\", color=" + color +
               ", headlabel=<>, fontsize=10, labelangle=45, labeldistance=2.0, labelfontcolor=black, dir=back];\n";
    }

    std::string
    loop_to_dot(hls::loop_node *ln) {
        auto sr = ln->subregion();
        std::ostringstream dot;
        dot << "subgraph cluster_loop {\n";

        std::set<jive::output *> back_outputs;
        std::set<jive::node *> top_nodes;
        for (int i = 0; i < sr->narguments(); ++i) {
            auto arg = sr->argument(i);
            // arguments without an input produce a cycle
            if(arg->input()== nullptr){
                back_outputs.insert(arg);
            }
        }

        for (auto node : jive::topdown_traverser(sr)) {
            if (auto sn = dynamic_cast<jive::simple_node *>(node)) {
                auto node_dot = node_to_dot(node);
                if(top_nodes.count(node)){
                    dot << "{rank=source; \n";
                    dot << node_dot;
                    dot << "}\n";
                } else{
                    dot << node_dot;
                }
                auto node_name = get_node_name(node);
                for (int i = 0; i < node->ninputs(); ++i) {
                    auto in_name = node_name + ":" + get_port_name(node->input(i));
                    assert(output_map.count(node->input(i)->origin()));
                    auto origin = output_map[node->input(i)->origin()];
                    // implement edge as back edge when it produces a cycle
                    bool back = back_outputs.count(node->input(i)->origin());
                    dot << edge(origin, in_name, node->input(i)->type(), back);
                }
            } else if (auto ln = dynamic_cast<hls::loop_node *>(node)) {
                dot << loop_to_dot(ln);
            } else {
                throw jlm::error("Unimplemented op (unexpected structural node) : " + node->operation().debug_string());
            }
        }
        // rank the predicate buffer as source
        dot << "{rank=source; " << get_node_name(jive::node_output::node(ln->predicate_buffer())) << "}\n";
        dot << "}\n";
        return dot.str();
    };

    void
    prepare_loop_out_port(hls::loop_node *ln){
        // make sure all outputs are translated an available (necessary for argument/result cycles)

        auto sr = ln->subregion();
        // just translate outputs
        for (auto node : jive::topdown_traverser(sr)) {
            if (auto sn = dynamic_cast<jive::simple_node *>(node)) {
                auto node_name = get_node_name(node);
                for (int i = 0; i < node->noutputs(); ++i) {
                    output_map[node->output(i)] = node_name + ":" + get_port_name(node->output(i));
                }
            } else if (auto oln = dynamic_cast<hls::loop_node *>(node)) {
                prepare_loop_out_port(oln);
            }
        }
        for (int i = 0; i < sr->narguments(); ++i) {
            auto arg = sr->argument(i);
            if(arg->input()!= nullptr){
                // map to input of loop
                output_map[arg] = output_map[arg->input()->origin()];
            } else{
                // map to end of loop (origin of associated result)
                output_map[arg] = output_map[sr->result(i)->origin()];
            }
        }
        for (int i = 0; i < ln->noutputs(); ++i) {
            auto out = ln->output(i);
            assert(out->results.size() == 1);
            output_map[out] = output_map[out->results.begin()->origin()];
        }
    }


    std::string
    subregion_to_dot(jive::region *sr) {
        std::ostringstream dot;
        dot << "digraph G {\n";
        for (size_t i = 0; i < sr->narguments(); ++i) {
            dot << argument_to_dot(sr->argument(i));
        }
        for (size_t i = 0; i < sr->nresults(); ++i) {
            dot << result_to_dot(sr->result(i));
        }


        for (int i = 0; i < sr->narguments(); ++i) {
            output_map[sr->argument(i)] = get_port_name(sr->argument(i));
        }
        for (auto node : jive::topdown_traverser(sr)) {
            if (auto sn = dynamic_cast<jive::simple_node *>(node)) {
                auto node_dot = node_to_dot(node);
                dot << node_dot;
                auto node_name = get_node_name(node);
                for (int i = 0; i < node->ninputs(); ++i) {
                    auto in_name = node_name + ":" + get_port_name(node->input(i));
                    assert(output_map.count(node->input(i)->origin()));
                    auto origin = output_map[node->input(i)->origin()];
                    dot << edge(origin, in_name, node->input(i)->type());
                }
                for (int i = 0; i < node->noutputs(); ++i) {
                    output_map[node->output(i)] = node_name + ":" + get_port_name(node->output(i));
                }
            } else if (auto ln = dynamic_cast<hls::loop_node *>(node)) {
                prepare_loop_out_port(ln);
                dot << loop_to_dot(ln);
            } else {
                throw jlm::error("Unimplemented op (unexpected structural node) : " + node->operation().debug_string());
            }
        }
        for (int i = 0; i < sr->nresults(); ++i) {
            auto origin = output_map[sr->result(i)->origin()];
            auto result = get_port_name(sr->result(i));
            dot << edge(origin, result, sr->result(i)->type());
        }
        dot << "}";
        return dot.str();
    }
};

class FirrtlHLS {
public:
    FirrtlHLS() {
    }

    void run(jlm::rvsdg_module &rm, llvm::Module *lm) {
        std::string firrtl;
        auto region = rm.graph()->root();
        if (auto ln = dynamic_cast<const jlm::lambda::node *>(region->nodes.begin().ptr())) {
            auto module = lambda_node_to_firrtl(ln);
            firrtl += indent(0) + "circuit " + module.name + ":\n";
        } else {
            throw jlm::error("Root should have only one lambda node now");
        }
        for (auto module: modules) {
            firrtl += module.firrtl;
        }
        auto source_file_name = lm->getSourceFileName();
        std::string file_name = source_file_name.substr(0, source_file_name.find_last_of('.')) + ".fir";
        std::ofstream firrtl_file;
        firrtl_file.open(file_name);
        firrtl_file << firrtl;
        firrtl_file.close();
    }

private:
    std::unordered_map<const jive::node *, std::string> node_map;
    std::vector<FirrtlModule> modules;


    std::string
    get_node_name(const jive::node *node) {
        auto found = node_map.find(node);
        if (found != node_map.end()) {
            return found->second;
        }
        auto new_name = jive::detail::strfmt("node_", node->operation().debug_string(), "_",  node_map.size());
        // remove chars that are not valid in firrtl module names
        std::replace_if(new_name.begin(), new_name.end(), isForbiddenChar, '_');
        node_map[node] = new_name;
        return new_name;
    }

//    std::string
//    get_node_port_name(jive::node_input *port){
//        return get_node_name(port->node())+"."+ get_port_name(port);
//    }
//
//    std::string
//    get_node_port_name(jive::node_output *port){
//        return get_node_name(port->node())+"."+ get_port_name(port);
//    }

    std::string
    get_port_name(jive::input *port) {
        std::string result;
        if (auto p = dynamic_cast<const jive::node_input *>(port)) {
            result += "i";
        } else if (auto p = dynamic_cast<const jive::result *>(port)) {
            result += "r";
        } else {
            throw std::logic_error(port->debug_string() + " not implemented!");
        }
        result += jive::detail::strfmt(port->index());
        return result;
    }

    std::string
    get_port_name(jive::output *port) {
        if (port == nullptr) {
            throw std::logic_error("nullptr!");
        }
        std::string result;
        if (auto p = dynamic_cast<const jive::argument *>(port)) {
            result += "a";
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

    std::string ready(jive::output *port) {
        return get_port_name(port) + ".ready";
    }

    std::string ready(jive::input *port) {
        return get_port_name(port) + ".ready";
    }

    std::string valid(jive::output *port) {
        return get_port_name(port) + ".valid";
    }

    std::string valid(jive::input *port) {
        return get_port_name(port) + ".valid";
    }

    std::string data(jive::output *port) {
        return get_port_name(port) + ".data";
    }

    std::string data(jive::input *port) {
        return get_port_name(port) + ".data";
    }

    std::string fire(jive::output *port) {
        return "and(" + valid(port) + ", " + ready(port) + ")";
    }

    std::string fire(jive::input *port) {
        return "and(" + valid(port) + ", " + ready(port) + ")";
    }

    std::string
    to_firrtl_type(const jive::type *type) {
        if (auto t = dynamic_cast<const jive::bittype *>(type)) {
            return "UInt<" + jive::detail::strfmt(t->nbits()) + ">";
        } else if (auto t = dynamic_cast<const jlm::ptrtype *>(type)) {
            return "UInt<64>";
        } else if (auto t = dynamic_cast<const jive::statetype *>(type)) {
            return "UInt<1>";
        } else {
            throw std::logic_error(type->debug_string() + " not implemented!");
        }
    }

    std::string mem_io() {
        std::ostringstream module;
        module << indent(2)
               << "output mem_req: {flip ready: UInt<1>, valid: UInt<1>, addr: UInt<64>, data: UInt<64>, write: UInt<1>, width: UInt<3>}\n";
        module << indent(2) << "input mem_res: {valid: UInt<1>, data: UInt<64>}\n";
        module << indent(2) << "mem_req.valid <= UInt<1>(0)\n";
        module << indent(2) << "mem_req.addr is invalid\n";
        module << indent(2) << "mem_req.write is invalid\n";
        module << indent(2) << "mem_req.data is invalid\n";
        module << indent(2) << "mem_req.width is invalid\n";
        return module.str();
    }

    std::string mux_mem(const std::vector<std::string> &mem_nodes) const {
        std::ostringstream mem;
        std::string previous_granted = "UInt<1>(0)";
        for (auto node_name:mem_nodes) {
            mem << indent(2) << node_name << ".mem_res.valid <= mem_res.valid\n";
            mem << indent(2) << node_name << ".mem_res.data <= mem_res.data\n";
            mem << indent(2) << node_name << ".mem_req.ready <= UInt<1>(0)\n";
            mem << indent(2) << "when and(not(" << previous_granted << ")," << node_name << ".mem_req.valid):\n";
            mem << indent(3) << node_name << ".mem_req.ready <= UInt<1>(1)\n";
            mem << indent(3) << "mem_req.addr <= " << node_name << ".mem_req.addr\n";
            mem << indent(3) << "mem_req.write <= " << node_name << ".mem_req.write\n";
            mem << indent(3) << "mem_req.valid <= UInt<1>(1)\n";
            mem << indent(3) << "mem_req.data <= " << node_name << ".mem_req.data\n";
            mem << indent(3) << "mem_req.width <= " << node_name << ".mem_req.width\n";
            mem << indent(2) << "node previous_granted_" << node_name << " = or(" << previous_granted << ", "
                << node_name << ".mem_req.ready)\n";
            previous_granted = "previous_granted_" + node_name;
        }
        return mem.str();
    }

    std::string module_header(const jive::node *node, bool has_mem_io = false) {
        std::ostringstream module;

        module << indent(1) + "module " + get_node_name(node) + ":\n";
        // io
        module << indent(2) + "; io" + "\n";
        module << indent(2) + "input clk: Clock" + "\n";
        module << indent(2) + "input reset: UInt<1>" + "\n";
        for (size_t i = 0; i < node->ninputs(); ++i) {
            module << indent(2) << "input i" << (i) << ": {flip ready: UInt<1>, valid: UInt<1>, data: " <<
                   to_firrtl_type(&node->input(i)->type()) << "}\n";
        }
        for (size_t i = 0; i < node->noutputs(); ++i) {
            module << indent(2) + "output o" + jive::detail::strfmt(i) +
                      ": {flip ready: UInt<1>, valid: UInt<1>, data: " +
                      to_firrtl_type(&node->output(i)->type()) + "}" + "\n";
        }
        if (has_mem_io) {
            module << mem_io();
        }

        return module.str();
    }

    FirrtlModule &mem_node_to_firrtl(const jive::simple_node *n) {
        std::string module_name = get_node_name(n);
        std::ostringstream module;
        module << module_header(n, true);

        bool store = dynamic_cast<const jlm::store_op *>(&(n->operation()));
        // registers
        module << indent(2) + "; registers" + "\n";
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) + "reg i" + jive::detail::strfmt(i) +
                      "_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))" + "\n";
            module << indent(2) + "reg i" + jive::detail::strfmt(i) + "_data_reg: " +
                      to_firrtl_type(&n->input(i)->type()) + ", clk" + "\n";
        }
        for (size_t i = 0; i < n->noutputs(); ++i) {
            module << indent(2) + "reg o" + jive::detail::strfmt(i) +
                      "_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))" + "\n";
            module << indent(2) + "reg o" + jive::detail::strfmt(i) + "_data_reg: " +
                      to_firrtl_type(&n->output(i)->type()) + ", clk" + "\n";
        }
        module << indent(2) + "reg sent_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))" + "\n";
        module << indent(2) + "node inputs_valid = ";
        std::string inputs_valids = "UInt<1>(1)"; // True by default
        for (size_t i = 0; i < n->ninputs(); ++i) {
            inputs_valids = "and(" + inputs_valids + ", i" + jive::detail::strfmt(i) + "_valid_reg)";
        }
        module << inputs_valids + "\n";
        module << indent(2) << "; mem request\n";
        module << indent(2) << "mem_req.valid <= and(inputs_valid, not(sent_reg))\n";
        module << indent(2) << "mem_req.addr <= i0_data_reg\n";
        int bit_width;
        if (store) {
            module << indent(2) << "mem_req.write <= UInt<1>(1)\n";
            module << indent(2) << "mem_req.data <= i1_data_reg\n";
            bit_width = dynamic_cast<const jive::bittype *>(&n->input(1)->type())->nbits();
        } else {
            module << indent(2) << "mem_req.write <= UInt<1>(0)\n";
            module << indent(2) << "mem_req.data is invalid\n";
            bit_width = dynamic_cast<const jive::bittype *>(&n->output(0)->type())->nbits();
        }
        int log2_bytes = log2(bit_width / 8);
        module << indent(2) << "mem_req.width <= UInt<4>(" << log2_bytes << ")\n";
        module << indent(2) << "when mem_req.ready:\n";
        module << indent(3) << "sent_reg <= UInt<1>(1)\n";

        module << indent(2) << "; mem response\n";
        module << indent(2) << "when and(sent_reg, mem_res.valid):\n";
        module << indent(3) << "sent_reg <= UInt<1>(0)\n";
        if (store) {
            module << indent(3) << "o0_valid_reg <= UInt<1>(1)\n";
            module << indent(3) << "o0_data_reg <= i2_data_reg\n";
            module << indent(3) + "i0_valid_reg <= UInt<1>(0)" + "\n";
            module << indent(3) + "i1_valid_reg <= UInt<1>(0)" + "\n";
            module << indent(3) + "i2_valid_reg <= UInt<1>(0)" + "\n";
        } else {
            module << indent(3) << "o0_valid_reg <= UInt<1>(1)\n";
            module << indent(3) << "o0_data_reg <= mem_res.data\n";
            module << indent(3) << "o1_valid_reg <= UInt<1>(1)\n";
            module << indent(3) << "o1_data_reg <= i1_data_reg\n";
            module << indent(3) + "i0_valid_reg <= UInt<1>(0)" + "\n";
            module << indent(3) + "i1_valid_reg <= UInt<1>(0)" + "\n";
        }
        // handshaking
        module << indent(2) << "; handshaking\n";
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) << ready(n->input(i)) << " <= not(i" << i << "_valid_reg)\n";
        }

        for (size_t i = 0; i < n->noutputs(); ++i) {
            auto out = n->output(i);
            module << indent(2) << valid(out) << " <= o" << i << "_valid_reg,\n";
            module << indent(2) << data(out) << " <= o" << i << "_data_reg\n";
            module << indent(2) << "when " << fire(out) << ":\n";
            module << indent(3) << "o" << i << "_valid_reg <= UInt<1>(0)\n";
        }
        for (size_t i = 0; i < n->ninputs(); ++i) {
            auto in = n->input(i);
            module << indent(2) << "when " << fire(in) << ":\n";
            module << indent(3) << "i" << i << "_valid_reg <= UInt<1>(1)" << "\n";
            module << indent(3) << "i" << i << "_data_reg <= " << data(in) << "\n";
        }

        modules.emplace_back(module_name, module.str(), true);
        return modules.back();
    }

    FirrtlModule &pred_buffer_to_firrtl(const jive::simple_node *n) {
        std::string module_name = get_node_name(n);
        std::ostringstream module;
        module << module_header(n);
        module << indent(2) << "; registers\n";
        // TODO: double buffer?
        // start initialized with a valid pred 0
        module << indent(2) << "reg buf_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(1)))\n";
        module << indent(2) << "reg buf_data_reg: " << to_firrtl_type(&n->output(0)->type())
               << ", clk  with: (reset => (reset, UInt<1>(0)))\n";
        auto o0 = n->output(0);
        module << indent(2) << valid(o0) << " <= buf_valid_reg\n";
        module << indent(2) << data(o0) << " <= buf_data_reg\n";
        auto i0 = n->input(0);
        module << indent(2) << ready(i0) << " <= not(buf_valid_reg)\n";
        // TODO: allow pass-through?
        module << indent(2) << "when " << fire(i0) << ":\n";
        module << indent(3) << "buf_valid_reg <= UInt<1>(1)\n";
        module << indent(3) << "buf_data_reg <= " << data(i0) << "\n";
        module << indent(2) << "when " << fire(o0) << ":\n";
        module << indent(3) << "buf_valid_reg <= UInt<1>(0)\n";
        modules.emplace_back(module_name, module.str(), false);
        return modules.back();
    }

    FirrtlModule &buffer_to_firrtl(const jive::simple_node *n) {
        std::string module_name = get_node_name(n);
        std::ostringstream module;
        module << module_header(n);
        module << indent(2) << "; registers\n";
        auto o = dynamic_cast<const hls::buffer_op *>(&(n->operation()));
        auto capacity = o->capacity;
        for (int i = 0; i < capacity; ++i) {
            module << indent(2) << "reg buf" << i << "_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))\n";
            module << indent(2) << "reg buf" << i << "_data_reg: " << to_firrtl_type(&n->output(0)->type())
                   << ", clk\n";
        }
        for (int i = 0; i <= capacity; ++i) {
            module << indent(2) << "wire in_consumed" << i << ": UInt<1>\n";
            module << indent(2) << "wire shift_out" << i << ": UInt<1>\n";
        }
        auto o0 = n->output(0);
        // connect out to buf0
        module << indent(2) << valid(o0) << " <= buf0_valid_reg\n";
        module << indent(2) << data(o0) << " <= buf0_data_reg\n";
        auto i0 = n->input(0);
        // buf is ready if last one is empty
        module << indent(2) << ready(i0) << " <= not(buf" << capacity - 1 << "_valid_reg)\n";

        // shift register with insertion into earliest free slot
        module << indent(2) << "shift_out0 <= " << fire(o0) << "\n";
        module << indent(2) << "in_consumed0 <= UInt<1>(0)\n";
        // invalid pseudo slot so we can use the same logic for the last slot
        module << indent(2) << "node buf" << capacity << "_valid_reg = UInt<1>(0)\n";
        module << indent(2) << "node buf" << capacity << "_data_reg = UInt<1>(0)\n";
        for (int i = 0; i < capacity; ++i) {
            module << indent(2) << "node will_be_empty" << i << " = or(shift_out" << i << ", not(buf" << i
                   << "_valid_reg))\n";
            module << indent(2) << "in_consumed" << i + 1 << " <= in_consumed" << i << "\n";
            module << indent(2) << "node in_available" << i << " = and(" << fire(i0) << ", not(in_consumed" << i
                   << "))\n";
            module << indent(2) << "shift_out" << i + 1 << " <= UInt<1>(0)\n";
            module << indent(2) << "when shift_out" << i << ":\n";
            module << indent(3) << "buf" << i << "_valid_reg <= UInt<1>(0)\n";
            module << indent(2) << "when will_be_empty" << i << ":\n";
            module << indent(3) << "when buf" << i + 1 << "_valid_reg:\n";
            module << indent(4) << "buf" << i << "_valid_reg <= UInt<1>(1)\n";
            module << indent(4) << "buf" << i << "_data_reg <= buf" << i + 1 << "_data_reg\n";
            module << indent(4) << "shift_out" << i + 1 << " <= UInt<1>(1)\n";
            module << indent(3) << "else when in_available" << i << ":\n";
            module << indent(4) << "in_consumed" << i + 1 << " <= UInt<1>(1)\n";
            module << indent(4) << "buf" << i << "_valid_reg <= UInt<1>(1)\n";
            module << indent(4) << "buf" << i << "_data_reg <= " << data(i0) << "\n";

        }
        modules.emplace_back(module_name, module.str(), false);
        return modules.back();
    }

    FirrtlModule &ndmux_to_firrtl(const jive::simple_node *n) {
        std::string module_name = get_node_name(n);
        std::ostringstream module;
        module << module_header(n);

        auto ipred = n->input(0);
        auto o0 = n->output(0);

        module << indent(2) << valid(o0) << " <= UInt<1>(0)\n";
        module << indent(2) << data(o0) << " is invalid\n";
        module << indent(2) << ready(ipred) << " <= UInt<1>(0)\n";

        for (int i = 1; i < n->ninputs(); ++i) {
            auto in = n->input(i);
            module << indent(2) << ready(in) << " <= UInt<1>(0)\n";
            module << indent(2) << "when and(" << valid(ipred) << ", eq(" << data(ipred) << ", UInt<64>(" << i - 1
                   << "))):\n";
            module << indent(3) << valid(o0) << " <= " << valid(in) << "\n";
            module << indent(3) << data(o0) << " <= " << data(in) << "\n";
            module << indent(3) << ready(in) << " <= " << ready(o0) << "\n";
            module << indent(3) << ready(ipred) << " <= and(" << ready(o0) << "," << valid(in) << ")\n";
        }
        modules.emplace_back(module_name, module.str(), false);
        return modules.back();
    }

    FirrtlModule &merge_to_firrtl(const jive::simple_node *n) {
        std::string module_name = get_node_name(n);
        std::ostringstream module;
        module << module_header(n);
        auto o0 = n->output(0);

        module << indent(2) << valid(o0) << " <= UInt<1>(0)\n";
        module << indent(2) << data(o0) << " is invalid\n";


        for (int i = 0; i < n->ninputs(); ++i) {
            auto in = n->input(i);
            module << indent(2) << ready(in) << " <= " << ready(o0) << "\n";
            module << indent(2) << "when " << valid(in) << ":\n";
            module << indent(3) << valid(o0) << " <= " << valid(in) << "\n";
            module << indent(3) << data(o0) << " <= " << data(in) << "\n";
        }
        modules.emplace_back(module_name, module.str(), false);
        return modules.back();
    }

    FirrtlModule &fork_to_firrtl(const jive::simple_node *n) {
        std::string module_name = get_node_name(n);
        std::ostringstream module;
        module << module_header(n);

        module << indent(2) << "; registers\n";
        for (int i = 0; i < n->noutputs(); ++i) {
            module << indent(2) << "reg out" << i << "_fired: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))\n";
        }

        auto i0 = n->input(0);
        std::string all_fired = "UInt<1>(1)"; // True by default
        // TODO: this is currently wrong - will repeat tokens for edges that are ready if not all are
        for (int i = 0; i < n->noutputs(); ++i) {
            auto out = n->output(i);
            module << indent(2) << valid(out) << " <= and(" << valid(i0) << ", not(out" << i << "_fired))\n";
            module << indent(2) << data(out) << " <= " << data(i0) << "\n";
            all_fired = "and(" + all_fired + ", or(" + ready(out) + ", out" + jive::detail::strfmt(i) + "_fired))";
        }
        module << indent(2) << "node all_fired = " << all_fired << "\n";
        module << indent(2) << ready(i0) << " <= all_fired\n";
        module << indent(2) << "when not(all_fired):\n";
        for (int i = 0; i < n->noutputs(); ++i) {
            module << indent(3) << "when " << fire(n->output(i)) << ":\n";
            module << indent(4) << "out" + jive::detail::strfmt(i) + "_fired <= UInt<1>(1)\n";
        }
        module << indent(2) << "else:\n";
        for (int i = 0; i < n->noutputs(); ++i) {
            module << indent(3) << "out" + jive::detail::strfmt(i) + "_fired <= UInt<1>(0)\n";
        }
        modules.emplace_back(module_name, module.str(), false);
        return modules.back();
    }

    FirrtlModule &sink_to_firrtl(const jive::simple_node *n) {
        std::string module_name = get_node_name(n);
        std::ostringstream module;
        module << module_header(n);
        auto i0 = n->input(0);
        module << indent(2) << ready(i0) << " <= UInt<1>(1)\n";

        modules.emplace_back(module_name, module.str(), false);
        return modules.back();
    }

    FirrtlModule &branch_to_firrtl(const jive::simple_node *n) {
        std::string module_name = get_node_name(n);
        std::ostringstream module;
        module << module_header(n);

        auto ipred = n->input(0);
        auto ival = n->input(1);

        module << indent(2) << ready(ival) << " <= UInt<1>(0)\n";
        module << indent(2) << ready(ipred) << " <= UInt<1>(0)\n";
        for (int i = 0; i < n->noutputs(); ++i) {
            auto out = n->output(i);
            module << indent(2) << valid(out) << " <= UInt<1>(0)\n";
            module << indent(2) << data(out) << " is invalid\n";
            module << indent(2) << "when and(" << valid(ipred) << ", eq(" << data(ipred) << ", UInt<64>(" << i
                   << "))):\n";
            module << indent(3) << ready(ival) << " <= " << ready(out) << "\n";
            module << indent(3) << ready(ipred) << " <= and(" << ready(out) << "," << valid(ival) << ")\n";
            module << indent(3) << valid(out) << " <= " << valid(ival) << "\n";
            module << indent(3) << data(out) << " <= " << data(ival) << "\n";
        }
        modules.emplace_back(module_name, module.str(), false);
        return modules.back();
    }

    FirrtlModule &trigger_to_firrtl(const jive::simple_node *n) {
        std::string module_name = get_node_name(n);
        std::ostringstream module;
        module << module_header(n);

        auto itrig = n->input(0);
        auto ival = n->input(1);
        auto out = n->output(0);
        // inputs have to both fire at the same time
        module << indent(2) << ready(itrig) << " <= and(" << ready(out) << "," << valid(ival) << ")\n";
        module << indent(2) << ready(ival) << " <= and(" << ready(out) << "," << valid(itrig) << ")\n";
        module << indent(2) << valid(out) << " <= and(" << valid(ival) << "," << valid(itrig) << ")\n";
        module << indent(2) << data(out) << " <= " << data(ival) << "\n";
        modules.emplace_back(module_name, module.str(), false);
        return modules.back();
    }

    std::string simple_op_to_firrtl(const jive::simple_node *n) {
        if (auto o = dynamic_cast<const jive::match_op *>(&(n->operation()))) {
            if (is_identity_mapping(*o)) {
                return data(n->input(0));
            } else {
                throw std::logic_error("non-identity mapping not implemented!");
            }
        } else if (auto o = dynamic_cast<const jive::bitsgt_op *>(&(n->operation()))) {
            return "gt(asSInt(" + data(n->input(0)) + "), asSInt(" + data(n->input(1)) + "))";
        } else if (auto o = dynamic_cast<const jlm::sext_op *>(&(n->operation()))) {
            return "asUInt(pad(asSInt(" + data(n->input(0)) + "), " + jive::detail::strfmt(o->ndstbits()) + "))";
        } else if (auto o = dynamic_cast<const jlm::trunc_op *>(&(n->operation()))) {
            return data(n->input(0));
        } else if (auto o = dynamic_cast<const jive::bitugt_op *>(&(n->operation()))) {
            return "gt(" + data(n->input(0)) + ", " + data(n->input(1)) + ")";
        } else if (auto o = dynamic_cast<const jive::bitult_op *>(&(n->operation()))) {
            return "lt(" + data(n->input(0)) + ", " + data(n->input(1)) + ")";
        } else if (auto o = dynamic_cast<const jive::bitslt_op *>(&(n->operation()))) {
            return "lt(asSInt(" + data(n->input(0)) + ", asSInt(" + data(n->input(1)) + "))";
        } else if (auto o = dynamic_cast<const jive::biteq_op *>(&(n->operation()))) {
            return "eq(" + data(n->input(0)) + ", " + data(n->input(1)) + ")";
        } else if (auto o = dynamic_cast<const jive::bitadd_op *>(&(n->operation()))) {
            return "add(" + data(n->input(0)) + ", " + data(n->input(1)) + ")";
        } else if (auto o = dynamic_cast<const jive::bitmul_op *>(&(n->operation()))) {
            return "mul(" + data(n->input(0)) + ", " + data(n->input(1)) + ")";
        } else if (auto o = dynamic_cast<const jive::bitsub_op *>(&(n->operation()))) {
            return "sub(" + data(n->input(0)) + ", " + data(n->input(1)) + ")";
        } else if (auto o = dynamic_cast<const jive::bitne_op *>(&(n->operation()))) {
            return "neq(" + data(n->input(0)) + ", " + data(n->input(1)) + ")";
        } else if (auto o = dynamic_cast<const jive::bitconstant_op *>(&(n->operation()))) {
            auto value = o->value();
            return jive::detail::strfmt("UInt<", value.nbits(), ">(", value.to_uint(), ")");
        } else if (auto o = dynamic_cast<const jlm::undef_constant_op *>(&(n->operation()))) {
            return "UInt<1>(0) ; TODO: Fix?";
        } else if (auto o = dynamic_cast<const jive::ctlconstant_op *>(&(n->operation()))) {
            return "UInt<1>(" + jive::detail::strfmt(o->value().alternative()) + ")";
        } else if (auto o = dynamic_cast<const jlm::getelementptr_op *>(&(n->operation()))) {
            assert(n->ninputs() == 2);
            const auto *pt = &o->pointee_type();
            int bits;
            if (auto bt = dynamic_cast<const jive::bittype *>(pt)) {
                bits = bt->nbits();
            } else {
                throw std::logic_error(pt->debug_string() + " pointer not implemented!");
            }
            int bytes = bits / 8;
            // TODO: fix
            return "add(" + data(n->input(0)) + ", mul(" + data(n->input(1)) + ", UInt<64>(" +
                   jive::detail::strfmt(bytes) + ")))";
        } else {
            throw std::logic_error(n->operation().debug_string() + " not implemented!");
        }
    }

    FirrtlModule &single_out_simple_node_to_firrtl(const jive::simple_node *n) {
        std::string module_name = get_node_name(n);
        std::ostringstream module;

        module << module_header(n);

        if (n->noutputs() != 1) {
            throw std::logic_error(n->operation().debug_string() + " has more than 1 output");
        }
        module << indent(2) << "; logic\n";
        module << indent(2) << " ; " << n->operation().debug_string() << "\n";
        auto op = simple_op_to_firrtl(n);
        module << indent(2) << data(n->output(0)) << " <= " << op;
        // handshaking
        module << indent(2) + "; handshaking" + "\n";

        auto out = n->output(0);

        std::string inputs_valids = "UInt<1>(1)"; // True by default
        for (size_t i = 0; i < n->ninputs(); ++i) {
            inputs_valids = "and(" + inputs_valids + ", " + valid(n->input(i)) + ")";
        }
        module << indent(2) << "node inputs_valid = " << inputs_valids << "\n";
        for (size_t i = 0; i < n->noutputs(); ++i) {
            module << indent(2) << valid(out) << " <= inputs_valid\n";
        }
        for (size_t i = 0; i < n->ninputs(); ++i) {
            module << indent(2) + ready(n->input(i)) + "<= and(" << ready(n->output(0)) << ", inputs_valid)\n";
        }

        modules.emplace_back(module_name, module.str(), false);
        return modules.back();
    }

    FirrtlModule
    node_to_firrtl(const jive::node *node, const int depth) {
        if (auto n = dynamic_cast<const jive::simple_node *>(node)) {
            if (auto o = dynamic_cast<const jlm::load_op *>(&(n->operation()))) {
                return mem_node_to_firrtl(n);
            } else if (auto o = dynamic_cast<const jlm::store_op *>(&(n->operation()))) {
                return mem_node_to_firrtl(n);
            } else if (auto o = dynamic_cast<const hls::predicate_buffer_op *>(&(n->operation()))) {
                return pred_buffer_to_firrtl(n);
            } else if (auto o = dynamic_cast<const hls::buffer_op *>(&(n->operation()))) {
                return buffer_to_firrtl(n);
            } else if (auto o = dynamic_cast<const hls::branch_op *>(&(n->operation()))) {
                return branch_to_firrtl(n);
            } else if (auto o = dynamic_cast<const hls::trigger_op *>(&(n->operation()))) {
                return trigger_to_firrtl(n);
            } else if (auto o = dynamic_cast<const hls::sink_op *>(&(n->operation()))) {
                return sink_to_firrtl(n);
            } else if (auto o = dynamic_cast<const hls::fork_op *>(&(n->operation()))) {
                return fork_to_firrtl(n);
            } else if (auto o = dynamic_cast<const hls::merge_op *>(&(n->operation()))) {
                return merge_to_firrtl(n);
            } else if (auto o = dynamic_cast<const hls::mux_op *>(&(n->operation()))) {
                if (o->discarding) {
                    throw std::logic_error("not implemented");
                } else {
                    return ndmux_to_firrtl(n);
                }
            }
            return single_out_simple_node_to_firrtl(n);
        } else {
            throw std::logic_error(node->operation().debug_string() + " not implemented!");
        }
    }

    static inline std::string
    indent(size_t depth) {
        return std::string(depth * 4, ' ');
    }

    FirrtlModule
    subregion_to_firrtl(const jive::region *sr, bool register_comb = false) {
        auto module_name = "subregion_mod" + jive::detail::strfmt(modules.size());
        std::string module = indent(1) + "module " + module_name + ":\n";
        // io
        module += indent(2) + "; io" + "\n";
        module += indent(2) + "input clk: Clock" + "\n";
        module += indent(2) + "input reset: UInt<1>" + "\n";
        for (size_t i = 0; i < sr->narguments(); ++i) {
            module +=
                    indent(2) + "input " + get_port_name(sr->argument(i)) +
                    ": {flip ready: UInt<1>, valid: UInt<1>, data: " +
                    to_firrtl_type(&sr->argument(i)->type()) + "}" + "\n";
        }
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(2) + "output " + get_port_name(sr->result(i)) +
                      ": {flip ready: UInt<1>, valid: UInt<1>, data: " +
                      to_firrtl_type(&sr->result(i)->type()) + "}" + "\n";
        }
        module += mem_io();
        module += indent(2) + "; instances" + "\n";
        std::unordered_map<jive::output *, std::string> out_port_map;
        std::unordered_map<const jive::node *, std::string> node_inst_map;
        std::vector<std::string> mem_nodes;
        for (int i = 0; i < sr->narguments(); ++i) {
            out_port_map[sr->argument(i)] = get_port_name(sr->argument(i));
        }
        // create node modules and ios first
        for (const auto &node : sr->nodes) {
            auto node_module = node_to_firrtl(&node, 2);
            std::string inst_name = node_module.name + "_inst";
            if (node_module.has_mem) {
                mem_nodes.push_back(inst_name);
            }
            module += indent(2) + "inst " + inst_name + " of " + node_module.name + "\n";
            for (int i = 0; i < node.noutputs(); ++i) {
                out_port_map[node.output(i)] = inst_name + "." + get_port_name(node.output(i));
            }
            node_inst_map[&node] = inst_name;
        }
        for (const auto &node : sr->nodes) {
            auto inst_name = node_inst_map[&node];
            module += indent(2) + inst_name + ".clk <= clk\n";
            module += indent(2) + inst_name + ".reset <= reset\n";
            for (int i = 0; i < node.ninputs(); ++i) {
                auto in_name = inst_name + "." + get_port_name(node.input(i));
                assert(out_port_map.count(node.input(i)->origin()));
                auto origin = out_port_map[node.input(i)->origin()];
                module += indent(2) + origin + ".ready <= " + in_name + ".ready\n";
                module += indent(2) + in_name + ".data <= " + origin + ".data\n";
                module += indent(2) + in_name + ".valid <= " + origin + ".valid\n";
            }
        }
        for (int i = 0; i < sr->nresults(); ++i) {
            auto origin = out_port_map[sr->result(i)->origin()];
            auto result = get_port_name(sr->result(i));
            module += indent(2) + origin + ".ready <= " + result + ".ready\n";
            module += indent(2) + result + ".data <= " + origin + ".data\n";
            module += indent(2) + result + ".valid <= " + origin + ".valid\n";
        }

        module += mux_mem(mem_nodes);

        modules.emplace_back(module_name, module, true);
        return modules.back();
    }

    FirrtlModule
    lambda_node_to_firrtl(const jlm::lambda::node *ln) {
        auto module_name = ln->name() + "_lambda_mod";
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
        module += mem_io();
        // registers
        module += indent(2) + "; registers" + "\n";
        for (size_t i = 0; i < sr->narguments(); ++i) {
            module += indent(2) + "reg i" + jive::detail::strfmt(i) +
                      "_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))" + "\n";
            module += indent(2) + "reg i" + jive::detail::strfmt(i) + "_data_reg: " +
                      to_firrtl_type(&sr->argument(i)->type()) + ", clk" + "\n";
        }
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(2) + "reg o" + jive::detail::strfmt(i) +
                      "_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))" + "\n";
            module += indent(2) + "reg o" + jive::detail::strfmt(i) + "_data_reg: " +
                      to_firrtl_type(&sr->result(i)->type()) + ", clk" + "\n";
        }
        module += indent(2) + "; instances" + "\n";
        auto sr_firrtl = subregion_to_firrtl(sr);
        module += indent(2) + "inst sr of " + sr_firrtl.name + "\n";
        module += indent(2) + "sr.clk <= clk\n";
        module += indent(2) + "sr.reset <= reset\n";
        for (size_t i = 0; i < sr->narguments(); ++i) {
            module += indent(2) + "sr." + valid(sr->argument(i)) + " <= i" + jive::detail::strfmt(i) + "_valid_reg\n";
            module += indent(2) + "sr." + data(sr->argument(i)) + " <= i" + jive::detail::strfmt(i) + "_data_reg\n";
            module += indent(2) + "when and(sr." + valid(sr->argument(i)) + ", sr." + ready(sr->argument(i)) + "):\n";
            module += indent(3) + "i" + jive::detail::strfmt(i) + "_valid_reg <= UInt<1>(0)" + "\n";
        }
        // logic
        module += indent(2) + "; logic" + "\n";
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(2) + "sr." + ready(sr->result(i)) + " <= not(o" + jive::detail::strfmt(i) +
                      "_valid_reg)" + "\n";
        }
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(2) + "when and(sr." + valid(sr->result(i)) + ", sr." + ready(sr->result(i)) + "):\n";
            module += indent(3) + "o" + jive::detail::strfmt(i) + "_valid_reg <= UInt<1>(1)" + "\n";
            module += indent(3) + "o" + jive::detail::strfmt(i) + "_data_reg <= sr." + get_port_name(sr->result(i)) +
                      ".data" + "\n";
        }
        std::string outputs_valids = "UInt<1>(1)"; // True by default
        for (size_t i = 0; i < sr->nresults(); ++i) {
            outputs_valids = "and(" + outputs_valids + ", o" + jive::detail::strfmt(i) + "_valid_reg)";
        }
        module += indent(2) + "node outputs_valid = " + outputs_valids + "\n";
        module += indent(2) + "o.valid <= outputs_valid\n";
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module +=
                    indent(2) + "o.data" + jive::detail::strfmt(i) + " <= o" + jive::detail::strfmt(i) + "_data_reg\n";
        }
        module += indent(2) + "when and(o.valid, o.ready):\n";
        for (size_t i = 0; i < sr->nresults(); ++i) {
            module += indent(3) + "o" + jive::detail::strfmt(i) + "_valid_reg <= UInt<1>(0)" + "\n";
        }
        std::string inputs_ready = "UInt<1>(1)"; // True by default
        for (size_t i = 0; i < sr->narguments(); ++i) {
            inputs_ready = "and(" + inputs_ready + ", not(i" + jive::detail::strfmt(i) + "_valid_reg))";
        }
        module += indent(2) + "node inputs_ready = " + inputs_ready + "\n";
        module += indent(2) + "i.ready <= inputs_ready\n";


        module += indent(2) + "when and(i.ready, i.valid):\n";
        for (size_t i = 0; i < sr->narguments(); ++i) {
            module += indent(3) + "i" + jive::detail::strfmt(i) + "_valid_reg <= UInt<1>(1)" + "\n";
            module +=
                    indent(3) + "i" + jive::detail::strfmt(i) + "_data_reg <= i.data" + jive::detail::strfmt(i) + "\n";
        }

        module += mux_mem({"sr",});
        modules.emplace_back(module_name, module, true);
        return modules.back();
    }
};


void
dump_xml(const std::string &source_file_name, std::unique_ptr<jlm::rvsdg_module> &rm, const std::string &suffix = "") {
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
    if (!lm) {
        err.print(argv[0], llvm::errs());
        exit(1);
    }
    auto source_file_name = lm->getSourceFileName();

    /* LLVM to JLM pass */
    auto jm = jlm::convert_module(*lm);
//    jlm::print(*jm, stdout);
    auto rm = jlm::construct_rvsdg(*jm, flags.sd);

    jlm::dne dne;
    jlm::cne cne;
    jlm::tginversion tgi;
    jlm::stats_descriptor sd;
    tgi.run(*rm, sd);
    dne.run(*rm, sd);
    cne.run(*rm, sd);

    dump_xml(source_file_name, rm);
    jive::view(rm->graph()->root(), stdout);
    hls::passes::hls_remove_unused_state(*rm);
    hls::passes::hls_add_triggers(*rm);
    hls::passes::hls_gamma_conv(*rm);
    hls::passes::hls_theta_conv(*rm);
//    dne.run(*rm, sd);
//    hls::passes::hls_add_sinks(*rm);
//    hls::passes::hls_add_forks(*rm);
//    hls::passes::hls_check_graph(*rm);
    jive::view(rm->graph()->root(), stdout);
    dump_xml(source_file_name, rm, "_hls");

//    FirrtlHLS hls;
//    hls.run(*rm, lm.get());
    DotHLS dhls;
    dhls.run(*rm, lm.get());
    return 0;
}