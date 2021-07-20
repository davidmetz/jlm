cmake_minimum_required(VERSION 3.16)

#add_subdirectory(../libjlm libjlm)

add_executable(jlc src/command.cpp src/cmdline.cpp src/jlc.cpp)

include_directories(jlc include)
target_link_libraries(jlc jlm)
install(TARGETS jlc jlm-opt)