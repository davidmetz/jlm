//
// Created by Magnus Sjalander 26 December 2021
//

#ifndef JLM_BACKEND_HLS_RHLS2FIRRTL_MLIRGEN_HPP
#define JLM_BACKEND_HLS_RHLS2FIRRTL_MLIRGEN_HPP

#include <jive/types/bitstring/type.hpp>
#include <jlm/ir/operators/operators.hpp>
#include <jive/types/bitstring/comparison.hpp>
#include <jlm/ir/operators/sext.hpp>
#include <jlm/ir/operators/getelementptr.hpp>
#include <jlm/ir/operators/load.hpp>
#include <jlm/ir/operators/store.hpp>
#include <jlm/ir/hls/hls.h>
#include <jive/rvsdg/traverser.hpp>

#include "base-hls.hpp"

namespace jlm {
	namespace hls{
		class MLIRGen : public BaseHLS {
			std::string
			extension() override {
				return ".fir";
			}
			std::string get_text(jlm::RvsdgModule &rm) override {return "";}
		public:
			void
			run(jlm::RvsdgModule &rvsdgModule) {
				std::cout << "This version of jlm-hls has not been compiled with support for the CIRCT backend\n";
			}
			private:
		};
	} // namespace hls
} // namespace jlm

#endif // JLM_BACKEND_HLS_RHLS2FIRRTL_MLIRGEN_HPP
