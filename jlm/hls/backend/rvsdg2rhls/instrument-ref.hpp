/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_INSTRUMENT_REF_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_INSTRUMENT_REF_HPP
#include <jlm/rvsdg/region.hpp>
#include <jlm/llvm/ir/RvsdgModule.hpp>

namespace jlm{
	namespace hls{
		void
		instrument_ref(jlm::RvsdgModule &rm);

		void
		instrument_ref
		(
			jlm::rvsdg::region * region,
			jlm::rvsdg::output * load_func,
			jlm::FunctionType & loadFunctionType,
			jlm::rvsdg::output * store_func,
			jlm::FunctionType & storeFunctionType,
			jlm::rvsdg::output * alloca_func,
			jlm::FunctionType & allocaFunctionType
		);
	}
}

#endif //JLM_BACKEND_HLS_RVSDG2RHLS_INSTRUMENT_REF_HPP
