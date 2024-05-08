/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_BRAM_POINTERS_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_BRAM_POINTERS_HPP

#include <jive/rvsdg/region.hpp>
#include <jlm/ir/RvsdgModule.hpp>


namespace jlm::hls {
    void
    bram_pointers(jive::region *region);

    void
    bram_pointers(jlm::RvsdgModule &rm);
}

#endif //JLM_BACKEND_HLS_RVSDG2RHLS_BRAM_POINTERS_HPP
