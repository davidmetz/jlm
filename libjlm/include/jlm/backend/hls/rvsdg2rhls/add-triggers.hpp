//
// Created by david on 7/2/21.
//

#ifndef JLM_LIBJLM_SRC_BACKEND_HLS_RVSDG2RHLS_ADD_TRIGGERS_HPP
#define JLM_LIBJLM_SRC_BACKEND_HLS_RVSDG2RHLS_ADD_TRIGGERS_HPP

#include <jive/rvsdg/region.hpp>
#include <jlm/ir/operators/lambda.hpp>
#include <jlm/ir/rvsdg-module.hpp>

namespace jlm {
	namespace hls {

		static jive::output *
		get_trigger(jive::region *region);

		static jlm::lambda::node *
		add_lambda_argument(jlm::lambda::node *ln, const jive::type *type);

		static void
		add_triggers(jive::region *region);

		void
		add_triggers(jlm::rvsdg_module &rm);
	}
}
#endif //JLM_LIBJLM_SRC_BACKEND_HLS_RVSDG2RHLS_ADD_TRIGGERS_HPP
