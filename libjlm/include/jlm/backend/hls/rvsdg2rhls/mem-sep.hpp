//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_MEM_SEP_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_MEM_SEP_HPP

#include <jlm/ir/RvsdgModule.hpp>

namespace jlm{
	namespace hls{

		void
		mem_sep(jive::region *region);

		void
		mem_sep(jlm::RvsdgModule &rm);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_MEM_SEP_HPP
