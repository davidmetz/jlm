/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_INSTRUMENT_REF_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_INSTRUMENT_REF_HPP
#include <jive/rvsdg/region.hpp>
#include <jlm/ir/RvsdgModule.hpp>

namespace jlm{
	namespace hls{
		void
		instrument_ref(jlm::RvsdgModule &rm);

		void
		instrument_ref(jive::region *region, jive::output * load_func, jive::output * store_func, jive::output *alloca_func);
	}
}

#endif //JLM_BACKEND_HLS_RVSDG2RHLS_INSTRUMENT_REF_HPP
