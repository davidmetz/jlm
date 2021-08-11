cmake_minimum_required(VERSION 3.16)

#add_subdirectory(../libjlm libjlm)

add_executable(jlc-hls src/command.cpp src/cmdline.cpp src/jlc.cpp)

include_directories(jlc-hls include)
target_link_libraries(jlc-hls jlm)
install(TARGETS jlc-hls jlm-hls)