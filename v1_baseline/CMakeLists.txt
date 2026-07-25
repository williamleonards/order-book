cmake_minimum_required(VERSION 3.20)
project(v1_baseline LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)    

enable_testing()

# Baseline order book. Header-only implementation (src/*.h) plus a single
# translation unit that runs the benchmark/test harness.
add_executable(v1_baseline_test test/test_main.cpp)

# Shared schema/streams/bench headers from common/.
target_link_libraries(v1_baseline_test PRIVATE common)

target_include_directories(v1_baseline_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_test(NAME v1_baseline COMMAND v1_baseline_test)
