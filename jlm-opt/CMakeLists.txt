cmake_minimum_required(VERSION 3.16)
#add_subdirectory(../libjlm libjlm)

add_executable(jlm-opt src/jlm-opt.cpp src/cmdline.cpp)

include_directories(jlm-opt include)
target_link_libraries(jlm-opt jlm)
install(TARGETS jlm-opt)