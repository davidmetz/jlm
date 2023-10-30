cmake_minimum_required(VERSION 3.16)

include(../IncludeLLVM.cmake)
include(../IncludeCIRCT.cmake)

add_executable(hls-check src/hls-check.cpp)

include_directories(jlm-opt include)

target_link_libraries(hls-check LLVM)
get_property(mlir_libs GLOBAL PROPERTY MLIR_ALL_LIBS)
target_link_libraries(hls-check ${mlir_libs})
message(STATUS "MLIR libs: ${mlir_libs}")
get_property(circt_libs GLOBAL PROPERTY CIRCT_ALL_LIBS)
target_link_libraries(hls-check ${circt_libs})
message(STATUS "CIRCT libs: ${circt_libs}")

install(TARGETS hls-check)