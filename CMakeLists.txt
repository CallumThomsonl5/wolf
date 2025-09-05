cmake_minimum_required(VERSION 3.15)
project(wolf VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} src/wolf.cpp src/timers.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE src)

option(BUILD_TESTS "Builds tests for the library" OFF)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
