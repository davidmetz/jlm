//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_INLINE_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_INLINE_HPP

#include <jlm/ir/rvsdg-module.hpp>
#include <jlm/ir/operators/lambda.hpp>
#include <jive/rvsdg/traverser.hpp>

namespace jlm{
	namespace hls{
		// adapted from conventional inlining pass
//		static void
//		hls_inline(const jlm::lambda::node * ln) {
//			for (auto node : jive::topdown_traverser(root)) {
//				if (!is<lambda::operation>(node))
//					continue;
//
//				auto snode = static_cast<const jive::structural_node*>(node);
//				auto consumers = find_consumers(snode);
//				if (consumers.size() == 1
//					&& dynamic_cast<const call_op*>(&consumers[0]->operation()))
//					inline_apply(snode, static_cast<jive::simple_node*>(consumers[0]));
//			}
//		}
	}
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_INLINE_HPP
