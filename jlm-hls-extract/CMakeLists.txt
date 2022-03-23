cmake_minimum_required(VERSION 3.16)

include(../IncludeLLVM.cmake)

add_executable(jlm-hls-extract jlm-hls-extract.cpp)

target_link_libraries(jlm-hls-extract jlm)

install(TARGETS jlm-hls-extract)