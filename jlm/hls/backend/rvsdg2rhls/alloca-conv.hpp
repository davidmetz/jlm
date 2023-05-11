/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_ALLOCA_CONV_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_ALLOCA_CONV_HPP

#include <jlm/rvsdg/theta.hpp>
#include <jlm/llvm/ir/RvsdgModule.hpp>

namespace jlm{
	namespace hls{
		void
		alloca_conv(jlm::rvsdg::region *region);

		void
		alloca_conv(jlm::RvsdgModule &rm);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_ALLOCA_CONV_HPP
