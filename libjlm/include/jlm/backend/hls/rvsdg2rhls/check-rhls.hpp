//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_CHECK_RHLS_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_CHECK_RHLS_HPP

#include <jlm/ir/rvsdg-module.hpp>

namespace jlm {
	namespace hls {
		static void
		check_rhls(jive::region *sr);

		void
		check_rhls(jlm::rvsdg_module &rm);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_CHECK_RHLS_HPP
