/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_IR_HLS_HLS_HPP
#define JLM_IR_HLS_HLS_HPP

#include <jive/rvsdg/operation.hpp>
#include <jive/rvsdg/control.hpp>
#include <jive/rvsdg/structural-node.hpp>
#include <jlm/common.hpp>
#include <jlm/ir/types.hpp>
#include <jive/rvsdg/substitution.hpp>

namespace jlm {
	namespace hls {
		class branch_op final : public jive::simple_op {
		private:
			branch_op(size_t nalternatives, const jive::type &type, bool loop)
					: jive::simple_op({jive::ctltype(nalternatives), type}, std::vector<jive::port>(nalternatives, type)), loop(loop){}

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
					jive::output &value,
					bool loop=false
			) {
				auto ctl = dynamic_cast<const jive::ctltype *>(&predicate.type());
				if (!ctl)
					throw jlm::error("Predicate needs to be a ctltype.");

				auto region = predicate.region();
				branch_op op(ctl->nalternatives(), value.type(), loop);
				return jive::simple_node::create_normalized(region, op, {&predicate, &value});
			}

			bool loop;//only used for dot output
		};

		class fork_op final : public jive::simple_op {
		public:
			virtual
			~fork_op() {}

			fork_op(size_t nalternatives, const jive::type &type)
					: jive::simple_op({type}, std::vector<jive::port>(nalternatives, type)) {}

			bool
			operator==(const jive::operation &other) const noexcept override {
				auto ot = dynamic_cast<const fork_op *>(&other);
				// check predicate and value
				return ot
					   && ot->argument(0).type() == argument(0).type()
					   && ot->nresults() == nresults();
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

			mux_op(size_t nalternatives, const jive::type &type, bool discarding, bool loop)
					: jive::simple_op(create_portvector(nalternatives, type), {type}), discarding(discarding), loop(loop) {}

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
					bool discarding,
					bool loop=false
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
				mux_op op(alternatives.size(), alternatives.front()->type(), discarding, loop);
				return jive::simple_node::create_normalized(region, op, operands);
			}


			bool discarding;
			bool loop; // used only for dot output
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

			buffer_op(const jive::type &type, size_t capacity, bool pass_through)
			: jive::simple_op({type}, {type}), capacity(capacity), pass_through(pass_through) {}

			bool
			operator==(const jive::operation &other) const noexcept override {
				auto ot = dynamic_cast<const buffer_op *>(&other);
				return ot
				&& ot->capacity == capacity
				&& ot->pass_through == pass_through
				&& ot->result(0).type() == result(0).type();
			}

			std::string
			debug_string() const override {
				return jive::detail::strfmt("HLS_BUF_",(pass_through ? "P_": ""),capacity);
			}

			std::unique_ptr<jive::operation>
			copy() const override {
				return std::unique_ptr<jive::operation>(new buffer_op(*this));
			}

			static std::vector<jive::output *>
			create(
					jive::output &value,
					size_t capacity,
					bool pass_through = false
			) {
				auto region = value.region();
				buffer_op op(value.type(), capacity, pass_through);
				return jive::simple_node::create_normalized(region, op, {&value});
			}

			size_t capacity;
			bool pass_through;
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
//				auto ot = dynamic_cast<const print_op *>(&other);
				// check predicate and value
//				return ot
//					   && ot->argument(0).type() == argument(0).type()
//					   && ot->result(0).type() == result(0).type();
				return false; // print nodes are intentionally distinct
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

        class backedge_argument;
        class backedge_result;
        class loop_node;

        class backedge_argument : public jive::argument {
            friend loop_node;
            friend backedge_result;
        public:
            ~backedge_argument() override= default;

            backedge_result * result(){
                return result_;
            }

        private:
            backedge_argument(
                    jive::region * region,
                    const jive::type & type)
                    : jive::argument(region, nullptr, type), result_(nullptr)
            {}

            static backedge_argument *
            create(
                    jive::region * region,
                    const jive::type & type)
            {
                auto argument = new backedge_argument(region, type);
                region->append_argument(argument);
                return argument;
            }
            backedge_result * result_;
        };

        class backedge_result : public jive::result {
            friend loop_node;
            friend backedge_argument;
        public:
            ~backedge_result() override= default;

            backedge_argument * argument(){
                return argument_;
            }

        private:
            backedge_result(jive::output * origin)
                    : jive::result(origin->region(), origin, nullptr, origin->port()), argument_(nullptr)
            {}

            static backedge_result *
            create(jive::output * origin)
            {
                auto result = new backedge_result(origin);
                origin->region()->append_result(result);
                return result;
            }
            backedge_argument * argument_;
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
			create(jive::region *parent, bool init = true);

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

			void set_predicate(jive::output *p);


			jive::structural_output *
			add_loopvar(jive::output *origin, jive::output **buffer = nullptr);

            backedge_argument * add_backedge(const jive::type & type);

			virtual loop_node *
			copy(jive::region *region, jive::substitution_map &smap) const override;
		};

        class load_op final : public jive::simple_op {
        public:
            virtual
            ~load_op() {}

            load_op(
                    const PointerType & pointerType,
                    size_t numStates)
                    : simple_op(CreateInPorts(pointerType, numStates), CreateOutPorts(pointerType, numStates)) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                auto ot = dynamic_cast<const load_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts(const PointerType & pointerType, size_t numStates)
            {
                std::vector<jive::port> ports(1, {pointerType});
//                ports.emplace_back(pointerType.GetElementType());
                std::vector<jive::port> states(numStates, {MemoryStateType::Create()});
                ports.insert(ports.end(), states.begin(), states.end());
                ports.emplace_back(pointerType.GetElementType()); // result
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(const PointerType & pointerType, size_t numStates)
            {
                std::vector<jive::port> ports(1, {pointerType.GetElementType()});
                std::vector<jive::port> states(numStates, {MemoryStateType::Create()});
                ports.insert(ports.end(), states.begin(), states.end());
                ports.emplace_back(pointerType); // addr
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_LOAD_"+ argument(narguments()-1).type().debug_string();
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new load_op(*this));
            }

            static std::vector<jive::output *>
            create(
                    jive::output &addr,
                    const std::vector<jive::output *> &states,
                    jive::output &load_result
            ) {
                auto region = addr.region();
                auto pointerType = dynamic_cast<const PointerType*>(&addr.type());
                load_op op(*pointerType, states.size());
                std::vector<jive::output*> inputs;
                inputs.push_back(&addr);
                inputs.insert(inputs.end(), states.begin(), states.end());
                inputs.push_back(&load_result);
                return jive::simple_node::create_normalized(region, op, inputs);
            }
        };

        class state_gate_op final : public jive::simple_op {
        public:
            virtual
            ~state_gate_op() {}

            state_gate_op(
                    const jive::type & type,
                    size_t numStates)
                    : simple_op(CreateInOutPorts(type, numStates), CreateInOutPorts(type, numStates)) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                auto ot = dynamic_cast<const state_gate_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInOutPorts(const jive::type & type, size_t numStates)
            {
                std::vector<jive::port> ports(1, {type});
                std::vector<jive::port> states(numStates, {MemoryStateType::Create()});
                ports.insert(ports.end(), states.begin(), states.end());
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_STATE_GATE_"+ argument(narguments()-1).type().debug_string();
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new state_gate_op(*this));
            }

            static std::vector<jive::output *>
            create(
                    jive::output &addr,
                    const std::vector<jive::output *> &states
            ) {
                auto region = addr.region();
                auto pointerType = dynamic_cast<const PointerType*>(&addr.type());
                state_gate_op op(*pointerType, states.size());
                std::vector<jive::output*> inputs;
                inputs.push_back(&addr);
                inputs.insert(inputs.end(), states.begin(), states.end());
                return jive::simple_node::create_normalized(region, op, inputs);
            }
        };

        class decoupled_load_op final : public jive::simple_op {
        public:
            virtual
            ~decoupled_load_op() {}

            decoupled_load_op(
                    const PointerType & pointerType)
                    : simple_op(CreateInPorts(pointerType), CreateOutPorts(pointerType)) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                auto ot = dynamic_cast<const decoupled_load_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts(const PointerType & pointerType)
            {
                std::vector<jive::port> ports(1, {pointerType});
                ports.emplace_back(pointerType.GetElementType()); // result
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(const PointerType & pointerType)
            {
                std::vector<jive::port> ports(1, {pointerType.GetElementType()});
                ports.emplace_back(pointerType); // addr
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_DEC_LOAD_"+ argument(narguments()-1).type().debug_string();
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new decoupled_load_op(*this));
            }

            static std::vector<jive::output *>
            create(
                    jive::output &addr,
                    jive::output &load_result
            ) {
                auto region = addr.region();
                auto pointerType = dynamic_cast<const PointerType*>(&addr.type());
                decoupled_load_op op(*pointerType);
                std::vector<jive::output*> inputs;
                inputs.push_back(&addr);
                inputs.push_back(&load_result);
                return jive::simple_node::create_normalized(region, op, inputs);
            }
        };

        class mem_resp_op final : public jive::simple_op {
        public:
            virtual
            ~mem_resp_op() {}

            mem_resp_op(
                    const std::vector<const jive::type *> &output_types)
                    : simple_op(CreateInPorts(), CreateOutPorts(output_types)) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                auto ot = dynamic_cast<const mem_resp_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts()
            {
//                return {jive::bit64};
                return {};
            }

            static std::vector<jive::port>
            CreateOutPorts(const std::vector<const jive::type *> &output_types)
            {
                std::vector<jive::port> ports;
                ports.reserve(output_types.size());
                for (auto t:output_types) {
                    ports.emplace_back(*t);
                }
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_MEM_RESP";
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new mem_resp_op(*this));
            }

            static std::vector<jive::output *>
            create(
//                    jive::output &result,
                    jive::region * region,
                    const std::vector<const jive::type *> &output_types
            ) {
//                auto region = result.region();
//                auto result_type = dynamic_cast<const jive::bittype*>(&result.type());
//                JLM_ASSERT(result_type && result_type->nbits()==64);
                mem_resp_op op(output_types);
//                return jive::simple_node::create_normalized(region, op, {&result});
                return jive::simple_node::create_normalized(region, op, {});
            }
        };

        class mem_req_op final : public jive::simple_op {
        public:
            virtual
            ~mem_req_op() {}

            mem_req_op(
                    const std::vector<const PointerType *> &load_types,
                    const std::vector<const PointerType *> &store_types)
                    : simple_op(CreateInPorts(load_types, store_types), CreateOutPorts()) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                auto ot = dynamic_cast<const mem_req_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts(
                    const std::vector<const PointerType *> &load_types,
                    const std::vector<const PointerType *> &store_types)
            {
                std::vector<jive::port> ports;
                for (auto lt:load_types) {
                    ports.emplace_back(*lt); // addr
                }
                for (auto st:store_types) {
                    ports.emplace_back(*st); // addr
                    ports.emplace_back(st->GetElementType()); // data
                }
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts()
            {
                std::vector<jive::port> ports;
//                ports.reserve(output_types.size());
//                for (auto t:output_types) {
//                    ports.emplace_back(*t);
//                }
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_MEM_REQ";
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new mem_req_op(*this));
            }

            static std::vector<jive::output *>
            create(
                    const std::vector<jive::output *> &load_operands,
                    const std::vector<jive::output *> &store_operands,
                    jive::region * region
            ) {
                std::vector<const PointerType *> load_types;
                for (size_t i = 0; i < load_operands.size(); ++i) {
                    auto pt = dynamic_cast<const PointerType*>(&load_operands[i]->type());
                    JLM_ASSERT(pt);
                    load_types.push_back(pt);
                }
                JLM_ASSERT(store_operands.size()%2==0);
                std::vector<const PointerType *> store_types;
                for (size_t i = 0; i < store_operands.size(); i+=2) {
                    auto pt = dynamic_cast<const PointerType*>(&store_operands[i]->type());
                    auto et = &store_operands[i+1]->type();
                    JLM_ASSERT(pt && et && pt->GetElementType() == *et);
                    store_types.push_back(pt);
                }
                mem_req_op op(load_types, store_types);
                std::vector<jive::output*> operands(load_operands);
                operands.insert(operands.end(), store_operands.begin(), store_operands.end());
                return jive::simple_node::create_normalized(region, op, operands);
            }
        };

        class store_op final : public jive::simple_op {
        public:
            virtual
            ~store_op() {}

            store_op(
                    const PointerType & pointerType,
                    size_t numStates)
                    : simple_op(CreateInPorts(pointerType, numStates), CreateOutPorts(pointerType, numStates)) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                auto ot = dynamic_cast<const store_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts(const PointerType & pointerType, size_t numStates)
            {
                std::vector<jive::port> ports({pointerType, pointerType.GetElementType()});
                std::vector<jive::port> states(numStates, {MemoryStateType::Create()});
                ports.insert(ports.end(), states.begin(), states.end());
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(const PointerType & pointerType, size_t numStates)
            {
                std::vector<jive::port> ports(numStates, {MemoryStateType::Create()});
                ports.emplace_back(pointerType); // addr
                ports.emplace_back(pointerType.GetElementType()); // data
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_STORE_"+ argument(narguments()-1).type().debug_string();
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new store_op(*this));
            }

            static std::vector<jive::output *>
            create(
                    jive::output &addr,
                    jive::output &value,
                    const std::vector<jive::output *> &states
            ) {
                auto region = addr.region();
                auto pointerType = dynamic_cast<const PointerType*>(&addr.type());
                store_op op(*pointerType, states.size());
                std::vector<jive::output*> inputs;
                inputs.push_back(&addr);
                inputs.push_back(&value);
                inputs.insert(inputs.end(), states.begin(), states.end());
                return jive::simple_node::create_normalized(region, op, inputs);
            }
        };
	}
}
#endif //JLM_IR_HLS_HLS_HPP
