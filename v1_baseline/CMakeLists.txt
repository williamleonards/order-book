cmake_minimum_required(VERSION 3.20)

project(v1_baseline LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)    
# for this implementation, use -O2 and default x86-64 architecture
add_compile_options(-O2)

# Main executable
add_executable(v1_baseline src/main.cpp)

target_link_libraries(v1_baseline PRIVATE common)

target_include_directories(v1_baseline PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Test executable
add_executable(v1_baseline_test test/test_main.cpp)

# Shared schema/streams/bench headers from common/.
target_link_libraries(v1_baseline_test PRIVATE common)

target_include_directories(v1_baseline_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src)

enable_testing()

add_test(NAME v1_baseline COMMAND v1_baseline_test)
