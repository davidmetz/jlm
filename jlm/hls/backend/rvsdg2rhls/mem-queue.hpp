//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_MEM_QUEUE_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_MEM_QUEUE_HPP

#include <jlm/llvm/ir/RvsdgModule.hpp>

namespace jlm{
	namespace hls{

		void
		mem_queue(jive::region *region);

		void
		mem_queue(jlm::RvsdgModule &rm);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_MEM_QUEUE_HPP
