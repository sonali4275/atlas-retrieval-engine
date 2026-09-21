cmake_minimum_required(VERSION 3.20)

project(atlas_retrieval_engine
    VERSION 0.2.0
    DESCRIPTION "A C++17 search engine core: positional inverted index, BM25, phrase and Boolean queries"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_EXTENSIONS OFF)

# Benchmarks are only meaningful with optimisation, so default to Release.
# (The tests do not use assert(), so NDEBUG in Release cannot disable them.)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ATLAS_BUILD_TESTS "Build the test suite" ON)
option(ATLAS_BUILD_BENCHMARKS "Build the benchmark executable" ON)
option(ATLAS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(ATLAS_SANITIZER "" CACHE STRING
    "Sanitizer to enable: 'address' (ASan + UBSan) or 'thread' (TSan)")
set_property(CACHE ATLAS_SANITIZER PROPERTY STRINGS "" address thread)

# ------------------------------------------------------------------
# Shared compiler settings (warnings + optional sanitizers)
# ------------------------------------------------------------------
add_library(atlas_options INTERFACE)

if(MSVC)
    target_compile_options(atlas_options INTERFACE /W4)
    if(ATLAS_WARNINGS_AS_ERRORS)
        target_compile_options(atlas_options INTERFACE /WX)
    endif()
else()
    target_compile_options(atlas_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
    if(ATLAS_WARNINGS_AS_ERRORS)
        target_compile_options(atlas_options INTERFACE -Werror)
    endif()

    if(ATLAS_SANITIZER STREQUAL "address")
        target_compile_options(atlas_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(atlas_options INTERFACE -fsanitize=address,undefined)
    elseif(ATLAS_SANITIZER STREQUAL "thread")
        target_compile_options(atlas_options INTERFACE
            -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(atlas_options INTERFACE -fsanitize=thread)
    elseif(NOT ATLAS_SANITIZER STREQUAL "")
        message(FATAL_ERROR
            "ATLAS_SANITIZER must be empty, 'address' or 'thread' (got '${ATLAS_SANITIZER}')")
    endif()
endif()

# ------------------------------------------------------------------
# Core library: compiled once, shared by the CLI, tests and benchmark
# ------------------------------------------------------------------
add_library(atlas_core STATIC
    src/tokenizer.cpp
    src/inverted_index.cpp
    src/query_processor.cpp
    src/index_serializer.cpp
    src/document_loader.cpp
)
target_include_directories(atlas_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(atlas_core PUBLIC atlas_options)

# ------------------------------------------------------------------
# Command-line search tool
# ------------------------------------------------------------------
add_executable(atlas src/main.cpp)
target_link_libraries(atlas PRIVATE atlas_core)

# ------------------------------------------------------------------
# Benchmark
# ------------------------------------------------------------------
if(ATLAS_BUILD_BENCHMARKS)
    add_executable(atlas_benchmark src/benchmark.cpp)
    target_link_libraries(atlas_benchmark PRIVATE atlas_core)
endif()

# ------------------------------------------------------------------
# Tests (registered with CTest so `ctest` runs them)
# ------------------------------------------------------------------
if(ATLAS_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)

    add_executable(atlas_tests tests/test_core.cpp)
    target_link_libraries(atlas_tests PRIVATE atlas_core Threads::Threads)

    add_test(NAME atlas_core_tests COMMAND atlas_tests)
    add_test(NAME atlas_cli_help COMMAND atlas --help)
endif()
