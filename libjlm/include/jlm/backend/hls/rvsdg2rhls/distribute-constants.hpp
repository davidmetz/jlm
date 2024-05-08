/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_LIBJLM_SRC_BACKEND_HLS_RVSDG2RHLS_DISTRIBUTE_CONSTANTS_HPP
#define JLM_LIBJLM_SRC_BACKEND_HLS_RVSDG2RHLS_DISTRIBUTE_CONSTANTS_HPP

#include <jive/rvsdg/region.hpp>
#include <jlm/ir/operators/lambda.hpp>
#include <jlm/ir/RvsdgModule.hpp>

namespace jlm {
	namespace hls {
		void
		distribute_constants(jive::region *region);

		void
		distribute_constants(jlm::RvsdgModule &rm);
	}
}
#endif //JLM_LIBJLM_SRC_BACKEND_HLS_RVSDG2RHLS_DISTRIBUTE_CONSTANTS_HPP
