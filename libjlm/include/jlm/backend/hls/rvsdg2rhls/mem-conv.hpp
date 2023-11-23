//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP

#include <jlm/ir/RvsdgModule.hpp>
#include "jlm/ir/operators/lambda.hpp"

namespace jlm{
	namespace hls{

		void
		mem_conv(jive::region *root);

        typedef std::vector<std::tuple<std::vector<jive::simple_node *>, std::vector<jive::simple_node *>, std::vector<jive::simple_node *>>> port_load_store_decouple;

		void
        trace_pointer_arguments(const jlm::lambda::node * ln, port_load_store_decouple &port_nodes);

		void
		mem_conv(jlm::RvsdgModule &rm);

        jive::output *
        route_response(jive::region *target, jive::output *response);

        jive::output *
        route_request(jive::region *target, jive::output *request);

        void generate_mem_port(const jlm::lambda::node * lambda, std::vector<jive::simple_node *> &load_nodes,
                               const std::vector<jive::simple_node *> &store_nodes, std::vector<jive::simple_node *> &decouple_nodes);
    }
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP
