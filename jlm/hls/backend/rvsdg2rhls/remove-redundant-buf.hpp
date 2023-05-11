/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_REMOVE_REDUNDANT_BUF_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_REMOVE_REDUNDANT_BUF_HPP

#include <jlm/rvsdg/gamma.hpp>
#include <jlm/llvm/ir/operators/lambda.hpp>
#include <jlm/llvm/ir/RvsdgModule.hpp>

namespace jlm {
	namespace hls {
		void
		remove_redundant_buf(jlm::RvsdgModule &rm);

		void
		remove_redundant_buf(jlm::rvsdg::region *region);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_REMOVE_REDUNDANT_BUF_HPP
