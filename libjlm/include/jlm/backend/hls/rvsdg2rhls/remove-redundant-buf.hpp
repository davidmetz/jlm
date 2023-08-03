/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_REMOVE_REDUNDANT_BUF_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_REMOVE_REDUNDANT_BUF_HPP

#include <jive/rvsdg/region.hpp>
#include <jive/rvsdg/gamma.hpp>
#include <jlm/ir/operators/lambda.hpp>
#include <jlm/ir/RvsdgModule.hpp>

namespace jlm {
	namespace hls {
		void
		remove_redundant_buf(jlm::RvsdgModule &rm);

		void
		remove_redundant_buf(jive::region *region);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_REMOVE_REDUNDANT_BUF_HPP
