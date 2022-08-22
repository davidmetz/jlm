//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_DAE_CONV_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_DAE_CONV_HPP

#include <jlm/ir/RvsdgModule.hpp>

namespace jlm{
	namespace hls{

		void
		dae_conv(jive::region *region);

		void
		dae_conv(jlm::RvsdgModule &rm);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_DAE_CONV_HPP
