cmake_minimum_required(VERSION 3.20)
project(FastStreamCompute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(faststreamcompute_core src/program.cpp
    src/record.cpp
    src/executor.cpp)
target_include_directories(faststreamcompute_core PUBLIC include)

add_executable(faststreamcompute src/main.cpp)
target_link_libraries(faststreamcompute PRIVATE faststreamcompute_core)

include(CTest)

if(BUILD_TESTING)
    add_executable(faststreamcompute_tests tests/test_executor.cpp)
    target_link_libraries(faststreamcompute_tests PRIVATE faststreamcompute_core)
    add_test(NAME reference_executor COMMAND faststreamcompute_tests)

    add_executable(faststreamcompute_spread_tests tests/test_spread.cpp)
    target_link_libraries(faststreamcompute_spread_tests PRIVATE faststreamcompute_core)
    add_test(NAME spread COMMAND faststreamcompute_spread_tests)
endif()
