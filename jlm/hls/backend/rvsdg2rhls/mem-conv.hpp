//
// Created by david on 7/2/21.
//

#ifndef JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP
#define JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP

#include <jlm/llvm/ir/RvsdgModule.hpp>
#include "jlm/llvm/ir/operators/lambda.hpp"

namespace jlm::hls{
    typedef std::vector<std::tuple<std::vector<jive::simple_node *>, std::vector<jive::simple_node *>, std::vector<jive::simple_node *>>> port_load_store_decouple;

    void
    trace_pointer_arguments(const jlm::lambda::node * ln, port_load_store_decouple &port_nodes);

    void
    MemoryConverter(jlm::RvsdgModule &rm);

    /**
     * @param lambda The lambda node for wich the load and store operations are to be connected to response (arguemnts) ports
     * @param argumentIndex The index of the reponse (argument) port to be connected
     * @param smap The substitution map for the lambda node
     * @param load_nodes The load nodes to be connected to the reponse port
     * @param store_nodes The store nodes to be connected to the reponse port
     * @param decouple_nodes The decouple nodes to be connected to the reponse port
     * @result The request output to which the memory operations are connected
     */
    jive::output *
    ConnectRequestResponseMemPorts(const jlm::lambda::node * lambda,
                        size_t argumentIndex,
                        jive::substitution_map & smap,
                        const std::vector<jive::simple_node *> & originalLoadNodes,
                        const std::vector<jive::simple_node *> & originalStoreNodes,
                        const std::vector<jive::simple_node *> & originalDecoupledNodes);

    jive::simple_node *
    ReplaceLoad(
        jive::substitution_map & smap,
        const jive::simple_node * originalLoad,
        jive::output * response);

    jive::simple_node *
    ReplaceStore(
        jive::substitution_map & smap,
        const jive::simple_node * originalStore);

    jive::output *
    route_response(jive::region *target, jive::output *response);

    jive::output *
    route_request(jive::region *target, jive::output *request);
}
#endif //JLM_BACKEND_HLS_RVSDG2RHLS_MEM_CONV_HPP
