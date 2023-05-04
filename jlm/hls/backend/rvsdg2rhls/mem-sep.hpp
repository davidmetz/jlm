//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_MEM_SEP_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_MEM_SEP_HPP

#include <jlm/llvm/ir/RvsdgModule.hpp>
#include <jlm/llvm/ir/operators/lambda.hpp>

namespace jlm{
	namespace hls{

		void
		mem_sep_independent(jive::region *region);

		void
		mem_sep_independent(jlm::RvsdgModule &rm);

		void
		mem_sep_argument(jive::region *region);

		void
		mem_sep_argument(jlm::RvsdgModule &rm);

        jive::argument *
        GetMemoryStateArgument(const jlm::lambda::node & lambda);

        jive::result *
        GetMemoryStateResult(const jlm::lambda::node & lambda);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_MEM_SEP_HPP
