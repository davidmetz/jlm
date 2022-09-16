/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_BACKEND_HLS_RHLS2FIRRTL_BASE_HLS_HPP
#define JLM_BACKEND_HLS_RHLS2FIRRTL_BASE_HLS_HPP

#include <jlm/ir/RvsdgModule.hpp>
#include <fstream>
#include <jlm/ir/operators/lambda.hpp>
#include <jlm/ir/operators/operators.hpp>
#include "jlm/ir/hls/hls.hpp"

namespace jlm {
	namespace hls {
		bool
		isForbiddenChar(char c);

		class BaseHLS {
		public:
			std::string
			run(jlm::RvsdgModule &rm) {
				assert(node_map.empty());
				// ensure consistent naming across runs
				create_node_names(get_hls_lambda(rm)->subregion());
				return get_text(rm);
			}

        private:
			virtual std::string
			extension() = 0;

		protected:
			std::unordered_map<const jive::node *, std::string> node_map;
			std::unordered_map<jive::output *, std::string> output_map;

			std::string
			get_node_name(const jive::node *node);

			static std::string
			get_port_name(jive::input *port);

			static std::string
			get_port_name(jive::output *port);

			const jlm::lambda::node *
			get_hls_lambda(jlm::RvsdgModule &rm);

			int
			JlmSize(const jive::type *type);

            void
			create_node_names(jive::region *r);

			virtual std::string
			get_text(jlm::RvsdgModule &rm) = 0;

			static std::string
			get_base_file_name(const RvsdgModule &rm);

            std::vector<jive::argument*> get_mem_resps(const jlm::lambda::node * lambda){
                std::vector<jive::argument*> mem_resps;
                for (size_t i = 0; i < lambda->subregion()->narguments(); ++i) {
                    auto arg = lambda->subregion()->argument(i);
                    if(dynamic_cast<const jlm::hls::bundletype *>(&arg->type())){
                        mem_resps.push_back(lambda->subregion()->argument(i));
                    }
                }
                return mem_resps;
            }

            std::vector<jive::result*> get_mem_reqs(const jlm::lambda::node * lambda){
                std::vector<jive::result*> mem_resps;
                for (size_t i = 0; i < lambda->subregion()->nresults(); ++i) {
                    if(dynamic_cast<const jlm::hls::bundletype *>(&lambda->subregion()->result(i)->type())){
                        mem_resps.push_back(lambda->subregion()->result(i));
                    }
                }
                return mem_resps;
            }

            std::vector<jive::argument*> get_reg_args(const jlm::lambda::node * lambda){
                std::vector<jive::argument*> args;
                for (size_t i = 0; i < lambda->subregion()->narguments(); ++i) {
                    auto argtype = &lambda->subregion()->argument(i)->type();
                    if(!dynamic_cast<const jlm::hls::bundletype *>(argtype) /*&& !dynamic_cast<const jive::statetype *>(argtype)*/){
                        args.push_back(lambda->subregion()->argument(i));
                    }
                }
                return args;
            }

            std::vector<jive::result*> get_reg_results(const jlm::lambda::node * lambda){
                std::vector<jive::result*> results;
                for (size_t i = 0; i < lambda->subregion()->nresults(); ++i) {
                    auto argtype = &lambda->subregion()->result(i)->type();
                    if(!dynamic_cast<const jlm::hls::bundletype *>(argtype) /*&& !dynamic_cast<const jive::statetype *>(argtype)*/){
                        results.push_back(lambda->subregion()->result(i));
                    }
                }
                return results;
            }

//            std::vector<jive::argument*> get_state_args(const jlm::lambda::node * lambda){
//                std::vector<jive::argument*> args;
//                for (size_t i = 0; i < lambda->subregion()->narguments(); ++i) {
//                    auto argtype = &lambda->subregion()->argument(i)->type();
//                    if(dynamic_cast<const jive::statetype *>(argtype)){
//                        args.push_back(lambda->subregion()->argument(i));
//                    }
//                }
//                return args;
//            }
//
//            std::vector<jive::result*> get_state_results(const jlm::lambda::node * lambda){
//                std::vector<jive::result*> results;
//                for (size_t i = 0; i < lambda->subregion()->nresults(); ++i) {
//                    auto argtype = &lambda->subregion()->result(i)->type();
//                    if(dynamic_cast<const jive::statetype *>(argtype)){
//                        results.push_back(lambda->subregion()->result(i));
//                    }
//                }
//                return results;
//            }
            
            
		};
	}
}


#endif //JLM_BACKEND_HLS_RHLS2FIRRTL_BASE_HLS_HPP
