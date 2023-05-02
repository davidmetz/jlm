/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_IR_HLS_HLS_HPP
#define JLM_IR_HLS_HLS_HPP

#include <jlm/llvm/ir/types.hpp>
#include <jlm/llvm/ir/operators/store.hpp>
#include <jlm/rvsdg/operation.hpp>
#include <jlm/rvsdg/control.hpp>
#include <jlm/rvsdg/structural-node.hpp>
#include <jlm/rvsdg/substitution.hpp>
#include <jlm/util/common.hpp>

#include <memory>
#include <utility>


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

		class loop_constant_buffer_op final : public jive::simple_op {
		public:
			virtual
			~loop_constant_buffer_op() {}

			loop_constant_buffer_op(const jive::ctltype &ctltype, const jive::type &type)
					: jive::simple_op({ctltype, type}, {type}) {}

			bool
			operator==(const jive::operation &other) const noexcept override {
				auto ot = dynamic_cast<const loop_constant_buffer_op *>(&other);
				return ot && ot->result(0).type() == result(0).type() && ot->argument(0).type() == argument(0).type();
			}

			std::string
			debug_string() const override {
				return "HLS_LOOP_CONST_BUF";
			}

			std::unique_ptr<jive::operation>
			copy() const override {
				return std::unique_ptr<jive::operation>(new loop_constant_buffer_op(*this));
			}

			static std::vector<jive::output *>
			create(
					jive::output &predicate,
					jive::output &value
			) {
				auto region = predicate.region();
				auto ctl = dynamic_cast<const jive::ctltype *>(&predicate.type());
				if (!ctl)
					throw jlm::error("Predicate needs to be a ctltype.");
				loop_constant_buffer_op op(*ctl, value.type());
				return jive::simple_node::create_normalized(region, op, {&predicate, &value});
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

            backedge_argument * argument() const {
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

			jive::node_output *_predicate_buffer;

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

			inline jive::node_output *
			predicate_buffer() const noexcept {
				return _predicate_buffer;
			}

			void set_predicate(jive::output *p);

			jive::structural_output *
			add_loopvar(jive::output *origin, jive::output **buffer = nullptr);

			jive::output *
			add_loopconst(jive::output *origin);

            backedge_argument * add_backedge(const jive::type & type);

			virtual loop_node *
			copy(jive::region *region, jive::substitution_map &smap) const override;
		};


        class bundletype final : public jive::valuetype {
        public:
            ~bundletype(){}

            bundletype(
                    const std::vector<std::pair<std::string, std::unique_ptr<jive::type>>> * elements)
                    : jive::valuetype()
                    , elements_(std::move(elements))
            {}

            bundletype(const bundletype &) = default;

            bundletype(bundletype &&) = delete;

            bundletype &
            operator=(const bundletype &) = delete;

            bundletype &
            operator=(bundletype &&) = delete;

            virtual bool
            operator==(const jive::type & other)  const noexcept override{
                auto type = dynamic_cast<const bundletype*>(&other);
                //TODO: better comparison?
                if(!type || type->elements_->size() != elements_->size()){
                    return false;
                }
                for (size_t i = 0; i < elements_->size(); ++i) {
                    if(type->elements_->at(i).first != elements_->at(i).first ||
                            *type->elements_->at(i).second != *elements_->at(i).second){
                        return false;
                    }
                }
                return true;
            };

            jive::type* get_element_type(std::string element) const{
                for (size_t i = 0; i < elements_->size(); ++i) {
                    if(elements_->at(i).first == element){
                        return elements_->at(i).second.get();
                    }
                }
                //TODO: do something different?
                return nullptr;
            }

            virtual std::unique_ptr<jive::type>
            copy() const override {
                return std::unique_ptr<jive::type>(new bundletype(*this));
            }

            virtual std::string
            debug_string()  const override {
                return "bundle";
            };

//        private:
            //TODO: fix memory leak
            const std::vector<std::pair<std::string, std::unique_ptr<jive::type>>> * elements_;
        };

        std::unique_ptr<bundletype> get_mem_req_type(const jive::valuetype & elementType, bool write);

        std::unique_ptr<bundletype> get_mem_res_type(const jive::valuetype & dataType);

        class load_op final : public jive::simple_op {
        public:
            virtual
            ~load_op() {}

            load_op(
                    const jive::valuetype & pointeeType,
                    size_t numStates)
                    : simple_op(CreateInPorts(pointeeType, numStates), CreateOutPorts(pointeeType, numStates)) {}

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
            CreateInPorts(const jive::valuetype & pointeeType, size_t numStates)
            {
                std::vector<jive::port> ports(1, { jlm::PointerType::Create() }); //addr
                std::vector<jive::port> states(numStates, {MemoryStateType::Create()});
                ports.insert(ports.end(), states.begin(), states.end());
                ports.emplace_back(pointeeType); // result
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(const jive::valuetype & pointeeType, size_t numStates)
            {
                std::vector<jive::port> ports(1, { pointeeType });
                std::vector<jive::port> states(numStates, {MemoryStateType::Create()});
                ports.insert(ports.end(), states.begin(), states.end());
                ports.emplace_back(jlm::PointerType::Create()); // addr
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
                load_op op(* dynamic_cast<const jive::valuetype *>(&load_result.type()), states.size());
                std::vector<jive::output*> inputs;
                inputs.push_back(&addr);
                inputs.insert(inputs.end(), states.begin(), states.end());
                inputs.push_back(&load_result);
                return jive::simple_node::create_normalized(region, op, inputs);
            }

            [[nodiscard]] const PointerType &
            GetPointerType() const noexcept
            {
                return *AssertedCast<const PointerType>(&argument(0).type());
            }

            [[nodiscard]] const jive::valuetype &
            GetLoadedType() const noexcept
            {
                return *AssertedCast<const jive::valuetype>(&result(0).type());
            }
        };

        class addr_queue_op final : public jive::simple_op {
        public:
            virtual
            ~addr_queue_op() {}

            addr_queue_op(
                    const PointerType & pointerType,
                    size_t capacity,
                    bool combinatorial)
                    : simple_op(CreateInPorts(pointerType), CreateOutPorts(pointerType)), combinatorial(combinatorial), capacity(capacity) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                auto ot = dynamic_cast<const addr_queue_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts(const PointerType & pointerType)
            {
                // check, enq
                std::vector<jive::port> ports(2, {pointerType});
                ports.emplace_back(MemoryStateType::Create()); // deq
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(const PointerType & pointerType)
            {
                return {pointerType};
            }

            std::string
            debug_string() const override {
                if(combinatorial){
                    return "HLS_ADDR_QUEUE_COMB_"+ argument(narguments()-1).type().debug_string();
                }
                return "HLS_ADDR_QUEUE_"+ argument(narguments()-1).type().debug_string();
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new addr_queue_op(*this));
            }

            static jive::output *
            create(
                    jive::output &check,
                    jive::output &enq,
                    jive::output &deq,
                    bool combinatorial,
                    size_t capacity = 10
            ) {
                auto region = check.region();
                auto pointerType = dynamic_cast<const PointerType*>(&check.type());
                addr_queue_op op(*pointerType, capacity, combinatorial);
                return jive::simple_node::create_normalized(region, op, {&check, &enq, &deq})[0];
            }


            bool combinatorial;
            size_t capacity;
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
                state_gate_op op(addr.type(), states.size());
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
                    const jive::valuetype & pointeeType)
                    : simple_op(CreateInPorts(pointeeType), CreateOutPorts(pointeeType)) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                auto ot = dynamic_cast<const decoupled_load_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts(const jive::valuetype & pointeeType)
            {
                std::vector<jive::port> ports(1, { PointerType() });
                ports.emplace_back(pointeeType); // result
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(const jive::valuetype & pointeeType)
            {
                std::vector<jive::port> ports(1, { pointeeType });
                ports.emplace_back(PointerType()); // addr
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
                decoupled_load_op op(*dynamic_cast<const jive::valuetype *>(&load_result.type()));
                std::vector<jive::output*> inputs;
                inputs.push_back(&addr);
                inputs.push_back(&load_result);
                return jive::simple_node::create_normalized(load_result.region(), op, inputs);
            }

            [[nodiscard]] const PointerType &
            GetPointerType() const noexcept
            {
                return *AssertedCast<const PointerType>(&argument(0).type());
            }

            [[nodiscard]] const jive::valuetype &
            GetLoadedType() const noexcept
            {
                return *AssertedCast<const jive::valuetype>(&result(0).type());
            }
        };

        class mem_resp_op final : public jive::simple_op {
        public:
            virtual
            ~mem_resp_op() {}

            mem_resp_op(
                    const std::vector<const jive::valuetype *> & output_types)
                    : simple_op(CreateInPorts(output_types), CreateOutPorts(output_types)) {}

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
            CreateInPorts(const std::vector<const jive::valuetype *> &output_types)
            {
                size_t max_width = 64;
                //TODO: calculate size onece JlmSize is moved
//                size_t max_width = 0;
//                for (auto tp:output_types) {
//                    auto sz = jlm::hls::BaseHLS::JlmSize(tp);
//                    max_width = sz>max_width?sz:max_width;
//                }
                std::vector<jive::port> ports;
                ports.emplace_back(get_mem_res_type(jive::bittype(max_width)));
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(const std::vector<const jive::valuetype *> &output_types)
            {
                std::vector<jive::port> ports;
                ports.reserve(output_types.size());
                for (auto outputType : output_types)
                {
                    ports.emplace_back(*outputType);
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
                    jive::output &result,
                    const std::vector<const jive::valuetype *> &output_types
            ) {
                auto region = result.region();
                // TODO: verify port here
//                auto result_type = dynamic_cast<const jive::bittype*>(&result.type());
//                JLM_ASSERT(result_type && result_type->nbits()==64);
                mem_resp_op op(output_types);
                return jive::simple_node::create_normalized(region, op, {&result});
            }
        };

        class mem_req_op final : public jive::simple_op {
        public:
            virtual
            ~mem_req_op()
            {
                delete LoadTypes_;
                delete StoreTypes_;
            }

            mem_req_op(
                    const std::vector<const jive::valuetype *> & load_types,
                    const std::vector<const jive::valuetype *> & store_types)
                    : simple_op(CreateInPorts(load_types, store_types), CreateOutPorts(load_types, store_types))
            {
                LoadTypes_ = new std::vector<std::unique_ptr<jive::type>>();
                StoreTypes_ = new std::vector<std::unique_ptr<jive::type>>();
                for (auto loadType : load_types)
                {
                    JLM_ASSERT(dynamic_cast<const jive::bittype *>(loadType) || dynamic_cast<const jlm::PointerType *>(loadType));
                    LoadTypes_->emplace_back(loadType->copy());
                }
                for (auto storeType : store_types)
                {
                    StoreTypes_->emplace_back(storeType->copy());
                }
            }

            mem_req_op(const mem_req_op& other)
            : simple_op(other)
            {
                LoadTypes_ = new std::vector<std::unique_ptr<jive::type>>();
                StoreTypes_ = new std::vector<std::unique_ptr<jive::type>>();
                for (auto &loadType : *other.LoadTypes_)
                {
                    LoadTypes_->push_back(loadType->copy());
                }
                for (auto &storeType : *other.StoreTypes_)
                {
                    StoreTypes_->push_back(storeType->copy());
                }
            }

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                auto ot = dynamic_cast<const mem_req_op *>(&other);
                // check predicate and value
                return ot
                       && ot->narguments() == narguments()
                       && (ot->narguments() == 0 || (ot->argument(1).type() == argument(1).type()))
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts(
                    const std::vector<const jive::valuetype *> & load_types,
                    const std::vector<const jive::valuetype *> & store_types)
            {
                std::vector<jive::port> ports;
                for (size_t i=0; i < load_types.size(); i++)
                {
                    ports.emplace_back(PointerType()); // addr
                }
                for (auto storeType : store_types)
                {
                    ports.emplace_back(PointerType()); // addr
                    ports.emplace_back(*storeType); // data
                }
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(
                    const std::vector<const jive::valuetype *> & load_types,
                    const std::vector<const jive::valuetype *> & store_types)
            {
                size_t max_width = 64;
                //TODO: fix once JlmSize is moved
//                size_t max_width = 0;
//                for (auto tp:load_types) {
//                    auto sz = jlm::hls::BaseHLS::JlmSize(tp);
//                    max_width = sz>max_width?sz:max_width;
//                }
//                for (auto tp:store_types) {
//                    auto sz = jlm::hls::BaseHLS::JlmSize(tp);
//                    max_width = sz>max_width?sz:max_width;
//                }
                std::vector<jive::port> ports;
                ports.emplace_back(get_mem_req_type(jive::bittype(max_width), !store_types.empty()));
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
                    const std::vector<jive::output *> & load_operands,
                    const std::vector<const jive::valuetype *> & loadTypes,
                    const std::vector<jive::output *> & store_operands,
                    jive::region * region
            ) {
                // Stores have both addr and data operand
                // But we are only interested in the data operand type
                JLM_ASSERT(store_operands.size()%2==0);
                std::vector<const jive::valuetype *> storeTypes;
                for (size_t i=0; i<store_operands.size(); i+=2) {
                    storeTypes.push_back(dynamic_cast<const jive::valuetype *>(&store_operands[i+1]->type()));
                }
                mem_req_op op(loadTypes, storeTypes);
                std::vector<jive::output*> operands(load_operands);
                operands.insert(operands.end(), store_operands.begin(), store_operands.end());
                return jive::simple_node::create_normalized(region, op, operands);
            }

            size_t get_nloads() const {
                return LoadTypes_->size();
            }

            std::vector<std::unique_ptr<jive::type>> *
            GetLoadTypes() const
            {
                return LoadTypes_;
            }

            std::vector<std::unique_ptr<jive::type>> *
            GetStoreTypes() const
            {
                return StoreTypes_;
            }

        private:
            std::vector<std::unique_ptr<jive::type>> * LoadTypes_;
            std::vector<std::unique_ptr<jive::type>> * StoreTypes_;
        };

        class store_op final : public jive::simple_op {
        public:
            virtual
            ~store_op() {}

            store_op(
                    const jive::valuetype & pointeeType,
                    size_t numStates)
                    : simple_op(CreateInPorts(pointeeType, numStates), CreateOutPorts(pointeeType, numStates)) {}

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
            CreateInPorts(const jive::valuetype & pointeeType, size_t numStates)
            {
                std::vector<jive::port> ports({ PointerType(), pointeeType });
                std::vector<jive::port> states(numStates, {MemoryStateType::Create()});
                ports.insert(ports.end(), states.begin(), states.end());
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(const jive::valuetype & pointeeType, size_t numStates)
            {
                std::vector<jive::port> ports(numStates, {MemoryStateType::Create()});
                ports.emplace_back(PointerType()); // addr
                ports.emplace_back(pointeeType); // data
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
                store_op op(*dynamic_cast<const jive::valuetype *>(&value.type()), states.size());
                std::vector<jive::output*> inputs;
                inputs.push_back(&addr);
                inputs.push_back(&value);
                inputs.insert(inputs.end(), states.begin(), states.end());
                return jive::simple_node::create_normalized(value.region(), op, inputs);
            }

            [[nodiscard]] const PointerType &
            GetPointerType() const noexcept
            {
                return *AssertedCast<const PointerType>(&argument(0).type());
            }

            [[nodiscard]] const jive::valuetype &
            GetStoredType() const noexcept
            {
                return *AssertedCast<const jive::valuetype>(&argument(1).type());
            }
        };

        class local_mem_op final : public jive::simple_op {
        public:
            virtual
            ~local_mem_op() {}

            local_mem_op(
                    const arraytype & at)
                    : simple_op({}, CreateOutPorts(at)) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                // auto ot = dynamic_cast<const local_mem_op *>(&other);
                // check predicate and value
                return false;
            }

            static std::vector<jive::port>
            CreateOutPorts(const arraytype & at)
            {
                std::vector<jive::port> ports(2, {at});
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_LOCAL_MEM_"+ result(0).type().debug_string();
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new local_mem_op(*this));
            }

            static std::vector<jive::output *>
            create(
                    const jlm::arraytype & at, jive::region * region
            ) {
                local_mem_op op(at);
                return jive::simple_node::create_normalized(region, op, {});
            }
        };
        
        class local_mem_resp_op final : public jive::simple_op {
        public:
            virtual
            ~local_mem_resp_op() {}

            local_mem_resp_op(
                    const jlm::arraytype &at,
                    size_t resp_count)
                    : simple_op({at}, CreateOutPorts(at, resp_count)) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                auto ot = dynamic_cast<const local_mem_resp_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }


            static std::vector<jive::port>
            CreateOutPorts(const jlm::arraytype &at,
                           size_t resp_count)
            {
                std::vector<jive::port> ports(resp_count, at.element_type());
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_LOCAL_MEM_RESP";
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new local_mem_resp_op(*this));
            }

            static std::vector<jive::output *>
            create(
                    jive::output &mem,
                    size_t resp_count
            ) {
                auto region = mem.region();
                auto at = dynamic_cast<const jlm::arraytype*>(&mem.type());
                local_mem_resp_op op(*at, resp_count);
                return jive::simple_node::create_normalized(region, op, {&mem});
            }
        };

        class local_load_op final : public jive::simple_op {
        public:
            virtual
            ~local_load_op() {}

            local_load_op(
                    const jive::valuetype & valuetype,
                    size_t numStates)
                    : simple_op(CreateInPorts(valuetype, numStates), CreateOutPorts(valuetype, numStates)) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                auto ot = dynamic_cast<const local_load_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts(const jive::valuetype & valuetype, size_t numStates)
            {
                std::vector<jive::port> ports(1, {jive::bit64});
                std::vector<jive::port> states(numStates, {MemoryStateType::Create()});
                ports.insert(ports.end(), states.begin(), states.end());
                ports.emplace_back(valuetype); // result
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(const jive::valuetype & valuetype, size_t numStates)
            {
                std::vector<jive::port> ports(1, {valuetype});
                std::vector<jive::port> states(numStates, {MemoryStateType::Create()});
                ports.insert(ports.end(), states.begin(), states.end());
                ports.emplace_back(jive::bit64); // addr
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_LOCAL_LOAD_"+ argument(narguments()-1).type().debug_string();
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new local_load_op(*this));
            }

            static std::vector<jive::output *>
            create(
                    jive::output &index,
                    const std::vector<jive::output *> &states,
                    jive::output &load_result
            ) {
                auto region = index.region();
                auto valuetype = dynamic_cast<const jive::valuetype*>(&load_result.type());
                local_load_op op(*valuetype, states.size());
                std::vector<jive::output*> inputs;
                inputs.push_back(&index);
                inputs.insert(inputs.end(), states.begin(), states.end());
                inputs.push_back(&load_result);
                return jive::simple_node::create_normalized(region, op, inputs);
            }

            [[nodiscard]] const jive::valuetype &
            GetLoadedType() const noexcept
            {
                return *AssertedCast<const jive::valuetype>(&result(0).type());
            }
        };

        class local_store_op final : public jive::simple_op {
        public:
            virtual
            ~local_store_op() {}

            local_store_op(
                    const jive::valuetype & valuetype,
                    size_t numStates)
                    : simple_op(CreateInPorts(valuetype, numStates), CreateOutPorts(valuetype, numStates)) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                auto ot = dynamic_cast<const local_store_op *>(&other);
                // check predicate and value
                return ot
                       && ot->argument(1).type() == argument(1).type()
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts(const jive::valuetype & valuetype, size_t numStates)
            {
                std::vector<jive::port> ports({jive::bit64, valuetype});
                std::vector<jive::port> states(numStates, {MemoryStateType::Create()});
                ports.insert(ports.end(), states.begin(), states.end());
                return ports;
            }

            static std::vector<jive::port>
            CreateOutPorts(const jive::valuetype & valuetype, size_t numStates)
            {
                std::vector<jive::port> ports(numStates, {MemoryStateType::Create()});
                ports.emplace_back(jive::bit64); // addr
                ports.emplace_back(valuetype); // data
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_LOCAL_STORE_"+ argument(narguments()-1).type().debug_string();
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new local_store_op(*this));
            }

            static std::vector<jive::output *>
            create(
                    jive::output &index,
                    jive::output &value,
                    const std::vector<jive::output *> &states
            ) {
                auto region = index.region();
                auto valuetype = dynamic_cast<const jive::valuetype*>(&value.type());
                local_store_op op(*valuetype, states.size());
                std::vector<jive::output*> inputs;
                inputs.push_back(&index);
                inputs.push_back(&value);
                inputs.insert(inputs.end(), states.begin(), states.end());
                return jive::simple_node::create_normalized(region, op, inputs);
            }

            [[nodiscard]] const jive::valuetype &
            GetStoredType() const noexcept
            {
                return *AssertedCast<const jive::valuetype>(&argument(1).type());
            }
        };

        class local_mem_req_op final : public jive::simple_op {
        public:
            virtual
            ~local_mem_req_op() {}

            local_mem_req_op(
                    const jlm::arraytype &at,
                    size_t load_cnt,
                    size_t store_cnt)
                    : simple_op(CreateInPorts(at, load_cnt, store_cnt), {}) {}

            bool
            operator==(const jive::operation &other) const noexcept override {
                // TODO:
                auto ot = dynamic_cast<const local_mem_req_op *>(&other);
                // check predicate and value
                return ot
                       && ot->narguments() == narguments()
                       && (ot->narguments() == 0 || (ot->argument(1).type() == argument(1).type()))
                       && ot->narguments() == narguments();
            }

            static std::vector<jive::port>
            CreateInPorts(
                    const jlm::arraytype &at,
                    size_t load_cnt,
                    size_t store_cnt)
            {
                std::vector<jive::port> ports(1, at);
                for (size_t i = 0; i < load_cnt; ++i) {
                    ports.emplace_back(jive::bit64); // addr
                }
                for (size_t i = 0; i < store_cnt; ++i) {
                    ports.emplace_back(jive::bit64); // addr
                    ports.emplace_back(at.element_type()); // data
                }
                return ports;
            }

            std::string
            debug_string() const override {
                return "HLS_LOCAL_MEM_REQ";
            }

            std::unique_ptr<jive::operation>
            copy() const override {
                return std::unique_ptr<jive::operation>(new local_mem_req_op(*this));
            }

            static std::vector<jive::output *>
            create(
                    jive::output &mem,
                    const std::vector<jive::output *> &load_operands,
                    const std::vector<jive::output *> &store_operands
            ) {
                auto region = mem.region();
                auto at = dynamic_cast<const jlm::arraytype*>(&mem.type());
                JLM_ASSERT(store_operands.size()%2==0);
                local_mem_req_op op(*at, load_operands.size(), store_operands.size()/2);
                std::vector<jive::output*> operands(1, &mem);
                operands.insert(operands.end(), load_operands.begin(), load_operands.end());
                operands.insert(operands.end(), store_operands.begin(), store_operands.end());
                return jive::simple_node::create_normalized(region, op, operands);
            }
        };

    }
}
#endif //JLM_IR_HLS_HLS_HPP
