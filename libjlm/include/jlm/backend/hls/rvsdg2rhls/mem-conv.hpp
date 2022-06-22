//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP

#include <jlm/ir/RvsdgModule.hpp>

namespace jlm{
	namespace hls{

		void
		mem_conv(jive::region *region);

		void
		mem_conv(jlm::RvsdgModule &rm);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP
