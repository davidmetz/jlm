//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_ADD_SINKS_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_ADD_SINKS_HPP

#include <jive/rvsdg/region.hpp>
#include <jlm/ir/rvsdg-module.hpp>

namespace jlm{
	namespace hls{
		static void
		add_sinks(jive::region *region);

		void
		add_sinks(jlm::rvsdg_module &rm);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_ADD_SINKS_HPP
