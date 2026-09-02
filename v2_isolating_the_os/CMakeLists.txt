cmake_minimum_required(VERSION 3.20)

project(v2_isolating_the_os LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)    
# for this implementation, use -O2 and default x86-64 architecture
add_compile_options(-O2)

# Main executable
add_executable(v2_isolating_the_os src/main.cpp)

target_link_libraries(v2_isolating_the_os PRIVATE common)

target_include_directories(v2_isolating_the_os PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Test executable
add_executable(v2_isolating_the_os_test test/test_main.cpp)

# Shared schema/streams/bench headers from common/.
target_link_libraries(v2_isolating_the_os_test PRIVATE common)

target_include_directories(v2_isolating_the_os_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src)

enable_testing()

add_test(NAME v2_isolating_the_os COMMAND v2_isolating_the_os_test)
