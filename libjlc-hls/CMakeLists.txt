cmake_minimum_required(VERSION 3.16)

include(../IncludeLLVM.cmake)

add_executable(jlc-hls src/command.cpp src/cmdline.cpp src/jlc-hls.cpp)
include_directories(jlc-hls include ../libjlc/include)
target_link_libraries(jlc-hls jlm)
install(TARGETS jlc-hls jlm-hls)