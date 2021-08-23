//
// Created by david on 8/19/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_ADD_PRINTS_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_ADD_PRINTS_HPP
#include <jive/rvsdg/region.hpp>
#include <jlm/ir/rvsdg-module.hpp>

namespace jlm{
	namespace hls{
		static void
		add_prints(jive::region *region);

		void
		add_prints(jlm::rvsdg_module &rm);

		void
		convert_prints(jlm::rvsdg_module &rm);

		static void
		convert_prints(jive::region *region, jive::output * printf);

		jive::output *
		route_to_region(jive::output * output, jive::region * region);
	}
}

#endif //JLM_BACKEND_HLS_RVSDG2RHLS_ADD_PRINTS_HPP
