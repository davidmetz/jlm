/*
 * Copyright 2021 David Metz <david.c.metz@ntnu.no>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_BACKEND_HLS_RHLS2FIRRTL_JSON_HLS_HPP
#define JLM_BACKEND_HLS_RHLS2FIRRTL_JSON_HLS_HPP


#include <jlm/rvsdg/bitstring/type.hpp>
#include <jlm/hls/backend/rhls2firrtl/base-hls.hpp>

namespace jlm {
	namespace hls {
		class JsonHLS : public BaseHLS {
			std::string
			extension() override {
				return ".json";
			}

			std::string
			get_text(jlm::RvsdgModule &rm) override;

		private:
		};
	}
}

#endif //JLM_BACKEND_HLS_RHLS2FIRRTL_JSON_HLS_HPP
