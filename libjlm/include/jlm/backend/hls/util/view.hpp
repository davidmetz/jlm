/*
 * Copyright 2024 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */


#ifndef JLM_BACKEND_HLS_UTIL_VIEW_HPP
#define JLM_BACKEND_HLS_UTIL_VIEW_HPP

#include <string>
#include <jlm/rvsdg/graph.hpp>
#include "jlm/ir/RvsdgModule.hpp"

namespace jlm::hls {
    std::string
    region_to_dot(jive::region * region);

    std::string
    to_dot(jive::region * region);

    void
    view_dot(jive::region * region, FILE * out);

    void
    dump_dot(jlm::RvsdgModule &rvsdgModule, const std::string &file_name);
}


#endif //JLM_BACKEND_HLS_UTIL_VIEW_HPP
