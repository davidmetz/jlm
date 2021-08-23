//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_GAMMA_CONV_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_GAMMA_CONV_HPP

#include <jive/rvsdg/gamma.hpp>
#include <jlm/ir/rvsdg-module.hpp>

namespace jlm{
	namespace hls{

		static bool gamma_can_be_spec(jive::gamma_node *gamma);

		static void gamma_conv_nonspec(jive::gamma_node *gamma);

		static void gamma_conv_spec(jive::gamma_node *gamma);

		static void
		gamma_conv(jive::region *region, bool allow_speculation=true);

		void
		gamma_conv(jlm::rvsdg_module &rm, bool allow_speculation=true);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_GAMMA_CONV_HPP
