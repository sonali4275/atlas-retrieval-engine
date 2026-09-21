cmake_minimum_required(VERSION 3.20)

project(atlas_retrieval_engine
    VERSION 0.1.0
    DESCRIPTION "A high-performance technical knowledge retrieval engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_EXTENSIONS OFF)

# ============================================================
# Main Atlas Retrieval Engine
# ============================================================

add_executable(atlas

    src/main.cpp

    src/tokenizer.cpp

    src/inverted_index.cpp

    src/query_processor.cpp

    src/index_serializer.cpp

    src/document_loader.cpp
)

target_include_directories(atlas
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)


# ============================================================
# Benchmark Executable
# ============================================================

add_executable(atlas_benchmark

    src/benchmark.cpp

    src/tokenizer.cpp

    src/inverted_index.cpp
)

target_include_directories(atlas_benchmark
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)


# ============================================================
# Compiler Warnings
# ============================================================

if(MSVC)

    target_compile_options(atlas
        PRIVATE
            /W4
        )

    target_compile_options(atlas_benchmark
        PRIVATE
            /W4
        )

else()

    target_compile_options(atlas
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )

    target_compile_options(atlas_benchmark
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )

endif()