//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_RHLS_DNE_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_RHLS_DNE_HPP

#include <jlm/ir/rvsdg-module.hpp>
#include <jlm/ir/hls/hls.h>

namespace jlm {
	namespace hls {
		static bool
		remove_unused_loop_outputs(hls::loop_node *ln);

		static bool
		remove_unused_loop_inputs(hls::loop_node *ln);


		static bool
		dne(jive::region *sr);

		void
		dne(jlm::rvsdg_module &rm);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_RHLS_DNE_HPP
