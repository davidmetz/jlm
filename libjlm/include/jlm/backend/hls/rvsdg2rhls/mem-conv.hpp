//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP

#include <jlm/ir/RvsdgModule.hpp>

namespace jlm{
	namespace hls{

		void
		mem_conv(jive::region *region);

		void
		mem_conv(jlm::RvsdgModule &rm);

        void generate_mem_port(jive::region *lambda_region, std::vector<jive::simple_node *> &load_nodes,
                               const std::vector<jive::simple_node *> &store_nodes);
    }
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP
