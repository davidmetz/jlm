//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_RVSDG2RHLS_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_RVSDG2RHLS_HPP

#include <jive/rvsdg/node.hpp>
#include <jive/types/bitstring/constant.hpp>
#include <jlm/ir/operators/operators.hpp>
#include <jlm/ir/rvsdg-module.hpp>

namespace jlm{
	namespace hls{
		static inline bool is_constant(const jive::node *node) {
			return jive::is<jive::bitconstant_op>(node) ||
				   jive::is<jlm::undef_constant_op>(node) ||
				   jive::is<jive::ctlconstant_op>(node);
		}

		std::unique_ptr<rvsdg_module>
		rvsdg2rhls(jlm::rvsdg_module &rm, std::string &function_name);

		void
		dump_ref(std::unique_ptr<jlm::rvsdg_module> &rhls);
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_RVSDG2RHLS_HPP
