//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_THETA_CONV_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_THETA_CONV_HPP

#include <jive/rvsdg/theta.hpp>
#include <jlm/ir/rvsdg-module.hpp>

namespace jlm{
	namespace hls{
		static void theta_conv(jive::theta_node *theta);

		static void
		theta_conv(jive::region *region);

		void
		theta_conv(jlm::rvsdg_module &rm);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_THETA_CONV_HPP
