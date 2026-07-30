cmake_minimum_required(VERSION 3.20)
project(common LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)    

add_library(common INTERFACE)

add_subdirectory(include)

# Any project linking to this library starts looking under include/
target_include_directories(common INTERFACE include)
target_compile_features(common INTERFACE cxx_std_23)
target_compile_options(common INTERFACE -Wpedantic -Wall -Wextra -Wconversion)