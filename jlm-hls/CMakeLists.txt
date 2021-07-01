cmake_minimum_required(VERSION 3.16)

project(jlm_hls)

add_subdirectory(../libjlm libjlm)

add_executable(jlm-hls jlm-hls.cpp)

target_link_libraries(jlm-hls jlm)