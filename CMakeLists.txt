cmake_minimum_required(VERSION 3.15)
project(wolf VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} src/wolf.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC include)

# test
add_executable(wolftest tests/wolftest.cpp)
target_link_libraries(wolftest ${PROJECT_NAME})
target_include_directories(wolftest PUBLIC include)
