cmake_minimum_required(VERSION 3.15)
project(wolf VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(${PROJECT_NAME}
    src/wolf.cpp
    src/iouring.cpp
    src/timers.cpp
    src/files.cpp
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

option(BUILD_TESTS "Builds tests for the library" OFF)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
