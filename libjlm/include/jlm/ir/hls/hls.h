//
// Created by david on 7/2/21.
//

#ifndef JLM_IR_HLS_HLS_HPP
#define JLM_IR_HLS_HLS_HPP

#include <jive/rvsdg/operation.hpp>
#include <jive/rvsdg/control.hpp>
#include <jive/rvsdg/structural-node.hpp>
#include <jlm/common.hpp>
#include <jive/rvsdg/substitution.hpp>

namespace jlm {
	namespace hls {
		class branch_op final : public jive::simple_op {
		private:
			branch_op(size_t nalternatives, const jive::type &type)
					: jive::simple_op({jive::ctltype(nalternatives), type}, std::vector<jive::port>(nalternatives, type)) {}

		public:
			virtual
			~branch_op() {}

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
				auto ot = dynamic_cast<const trigger_op *>(&other);
				// check predicate and value
				return ot
					   && ot->argument(1).type() == argument(1).type()
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

		class print_op final : public jive::simple_op {
		private:
			size_t _id;
		public:
			virtual
			~print_op() {}

			print_op(const jive::type &type)
			: jive::simple_op({type}, {type}) {
				static size_t common_id{0};
				_id = common_id++;
			}

			bool
			operator==(const jive::operation &other) const noexcept override {
				auto ot = dynamic_cast<const print_op *>(&other);
				// check predicate and value
				return ot
					   && ot->argument(0).type() == argument(0).type()
					   && ot->result(0).type() == result(0).type();
			}

			std::string
			debug_string() const override {
				return jive::detail::strfmt("HLS_PRINT_", _id);
			}

			size_t
			id() const {
				return _id;
			}

			std::unique_ptr<jive::operation>
			copy() const override {
				return std::unique_ptr<jive::operation>(new print_op(*this));
			}

			static std::vector<jive::output *>
			create(
					jive::output &value
			) {

				auto region = value.region();
				print_op op(value.type());
				return jive::simple_node::create_normalized(region, op, {&value});
			}
		};

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
				//TODO: proper connection between argument_loop and result_loop
				auto result_out = jive::result::create(subregion(), branch[0], output, origin->type());
				auto result_loop = jive::result::create(subregion(), branch[1], nullptr, origin->type());
				return output;
			}

			virtual loop_node *
			copy(jive::region *region, jive::substitution_map &smap) const override {
				// TODO: fix this - broken - triggered if order is swapped in theta conversion pass
				auto nf = graph()->node_normal_form(typeid(jive::operation));
				nf->set_mutable(false);

				jive::substitution_map rmap;
				auto loop = create(region, false);

				for (int i = 0; i < ninputs(); ++i) {
					auto in_origin = smap.lookup(input(i)->origin());
					jive::structural_input::create(loop, in_origin, in_origin->type());
					rmap.insert(input(i), loop->input(i));
				}
				for (int i = 0; i < noutputs(); ++i) {
					auto out = jive::structural_output::create(loop, output(i)->type());
					rmap.insert(output(i), out);
					smap.insert(output(i), out);
				}

				subregion()->copy(loop->subregion(), rmap, true, true);
				loop->_predicate_buffer = rmap.lookup(_predicate_buffer);
				nf->set_mutable(true);
				return loop;
			}
		};
	}
}
#endif //JLM_IR_HLS_HLS_HPP
