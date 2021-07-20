cmake_minimum_required(VERSION 3.16)

find_package(LLVM 10 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_library(jlm STATIC
        src/common.cpp

        src/backend/llvm/jlm2llvm/instruction.cpp
        src/backend/llvm/jlm2llvm/jlm2llvm.cpp
        src/backend/llvm/jlm2llvm/type.cpp
        src/backend/llvm/rvsdg2jlm/rvsdg2jlm.cpp

        src/driver/command.cpp
        src/driver/passgraph.cpp

        src/frontend/llvm/jlm2rvsdg/module.cpp
        src/frontend/llvm/jlm2rvsdg/restructuring.cpp
        src/frontend/llvm/llvm2jlm/instruction.cpp
        src/frontend/llvm/llvm2jlm/module.cpp
        src/frontend/llvm/llvm2jlm/type.cpp

        src/ir/aggregation.cpp
        src/ir/annotation.cpp
        src/ir/attribute.cpp
        src/ir/basic-block.cpp
        src/ir/cfg.cpp
        src/ir/cfg-structure.cpp
        src/ir/cfg-node.cpp
        src/ir/domtree.cpp
        src/ir/ipgraph.cpp
        src/ir/ipgraph-module.cpp
        src/ir/operators/alloca.cpp
        src/ir/operators/call.cpp
        src/ir/operators/delta.cpp
        src/ir/operators/getelementptr.cpp
        src/ir/operators/lambda.cpp
        src/ir/operators/load.cpp
        src/ir/operators/operators.cpp
        src/ir/operators/sext.cpp
        src/ir/operators/store.cpp
        src/ir/print.cpp
        src/ir/rvsdg-module.cpp
        src/ir/ssa.cpp
        src/ir/tac.cpp
        src/ir/types.cpp
        src/ir/variable.cpp

        src/opt/cne.cpp
        src/opt/dne.cpp
        src/opt/inlining.cpp
        src/opt/invariance.cpp
        src/opt/inversion.cpp
        src/opt/optimization.cpp
        src/opt/pull.cpp
        src/opt/push.cpp
        src/opt/reduction.cpp
        src/opt/unroll.cpp

        src/util/stats.cpp

        src/ir/hls/hls.cpp

        src/backend/hls/rvsdg2rhls/add-triggers.cpp
        src/backend/hls/rvsdg2rhls/gamma-conv.cpp
        src/backend/hls/rvsdg2rhls/theta-conv.cpp
        src/backend/hls/rvsdg2rhls/add-sinks.cpp
        src/backend/hls/rvsdg2rhls/add-forks.cpp
        src/backend/hls/rvsdg2rhls/check-rhls.cpp
        src/backend/hls/rvsdg2rhls/rhls-dne.cpp
        src/backend/hls/rvsdg2rhls/remove-unused-state.cpp
        src/backend/hls/rvsdg2rhls/inline.cpp
        src/backend/hls/rvsdg2rhls/rvsdg2rhls.cpp

        src/backend/hls/rhls2firrtl/base-hls.cpp
        src/backend/hls/rhls2firrtl/dot-hls.cpp
        src/backend/hls/rhls2firrtl/firrtl-hls.cpp
        )

target_link_libraries(jlm jive)
target_include_directories(jlm PUBLIC include)

llvm_map_components_to_libnames(llvm_libs support core irreader)
target_link_libraries(jlm ${llvm_libs})