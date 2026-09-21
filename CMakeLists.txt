cmake_minimum_required(VERSION 3.20)
project(FastStreamCompute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(faststreamcompute_core src/program.cpp
    src/record.cpp
    src/reference_executor.cpp
    src/bytecode_executor.cpp)
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

    add_executable(faststreamcompute_bytecode_tests tests/test_bytecode_executor.cpp)
    target_link_libraries(faststreamcompute_bytecode_tests PRIVATE faststreamcompute_core)
    add_test(NAME bytecode_matches_reference COMMAND faststreamcompute_bytecode_tests)

    add_executable(faststreamcompute_program_validation_tests tests/test_program_validation.cpp)
    target_link_libraries(faststreamcompute_program_validation_tests PRIVATE faststreamcompute_core)
    add_test(NAME program_validation COMMAND faststreamcompute_program_validation_tests)
endif()
