cmake_minimum_required(VERSION 3.20)
project(FastStreamCompute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(faststreamcompute_core src/program.cpp
    src/record.cpp
    src/reference_executor.cpp
    src/bytecode_executor.cpp
    src/native_kernel.cpp
    src/record_generator.cpp
    src/program_optimiser.cpp)
target_include_directories(faststreamcompute_core PUBLIC include)

add_executable(faststreamcompute src/main.cpp)
target_link_libraries(faststreamcompute PRIVATE faststreamcompute_core)

option(FSC_BUILD_BENCHMARKS "Download and build Google Benchmark support" OFF)
option(FSC_DOWNLOAD_FLAMEGRAPH "Download the FlameGraph scripts" ON)

if(FSC_DOWNLOAD_FLAMEGRAPH)
    include(FetchContent)
    FetchContent_Declare(flamegraph
        GIT_REPOSITORY https://github.com/brendangregg/FlameGraph.git
        GIT_TAG 41fee1f99f9276008b7cd112fca19dc3ea84ac32
        # One location for the wrapper, regardless of the CMake build directory.
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/build/_deps/flamegraph-src"
    )
    FetchContent_MakeAvailable(flamegraph)
endif()

if(FSC_BUILD_BENCHMARKS)
    include(FetchContent)

    # Build the measurement library, not its own tests or installation rules.
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        # Google Benchmark v1.9.5, pinned to its release commit.
        GIT_TAG 192ef10025eb2c4cdd392bc502f0c852196baa48
    )
    FetchContent_MakeAvailable(googlebenchmark)

    add_executable(faststreamcompute_bench
        bench/bench_execution.cpp
        bench/bench_fusion.cpp
        bench/bench_program_optimisation.cpp
    )

    target_link_libraries(faststreamcompute_bench PRIVATE
        faststreamcompute_core
        benchmark::benchmark_main
    )

    # The latency harness uses GNU-style assembly and x86 timestamp intrinsics.
    if((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR MINGW) AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_executable(faststreamcompute_latency bench/bench_latency_harness.cpp)
        target_link_libraries(faststreamcompute_latency PRIVATE faststreamcompute_core)
    endif()
endif()

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

    add_executable(faststreamcompute_batch_tests tests/test_batch_executor.cpp)
    target_link_libraries(faststreamcompute_batch_tests PRIVATE faststreamcompute_core)
    add_test(NAME batch_executor COMMAND faststreamcompute_batch_tests)

    add_executable(faststreamcompute_chunked_tests tests/test_chunked_executor.cpp)
    target_link_libraries(faststreamcompute_chunked_tests PRIVATE faststreamcompute_core)
    add_test(NAME chunked_executor COMMAND faststreamcompute_chunked_tests)

    add_executable(faststreamcompute_multi_output_tests tests/test_multi_output.cpp)
    target_link_libraries(faststreamcompute_multi_output_tests PRIVATE faststreamcompute_core)
    add_test(NAME multi_output COMMAND faststreamcompute_multi_output_tests)

    add_executable(faststreamcompute_program_validation_tests tests/test_program_validation.cpp)
    target_link_libraries(faststreamcompute_program_validation_tests PRIVATE faststreamcompute_core)
    add_test(NAME program_validation COMMAND faststreamcompute_program_validation_tests)

    add_executable(faststreamcompute_native_kernel_tests tests/test_native_kernel.cpp)
    target_link_libraries(faststreamcompute_native_kernel_tests PRIVATE faststreamcompute_core)
    add_test(NAME native_kernels COMMAND faststreamcompute_native_kernel_tests)

    add_executable(faststreamcompute_optimiser_tests tests/test_program_optimiser.cpp)
    target_link_libraries(faststreamcompute_optimiser_tests PRIVATE faststreamcompute_core)
    add_test(NAME program_optimiser COMMAND faststreamcompute_optimiser_tests)
endif()
