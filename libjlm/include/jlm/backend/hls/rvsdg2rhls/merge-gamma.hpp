/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_MERGE_GAMMA_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_MERGE_GAMMA_HPP

#include <jlm/rvsdg/gamma.hpp>
#include <jlm/ir/RvsdgModule.hpp>

namespace jlm{
	namespace hls{
		void
		merge_gamma(jive::region *region);

		void
		merge_gamma(jlm::RvsdgModule &rm);

        bool merge_gamma(jive::gamma_node *gamma);
    }
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_MERGE_GAMMA_HPP
