# Google Benchmark setup
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/benchmark/CMakeLists.txt")
    message(STATUS "QuantEngine: Using vendored Google Benchmark from third_party/benchmark")
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_FORCE_WERROR OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../third_party/benchmark" "${CMAKE_CURRENT_BINARY_DIR}/third_party/benchmark" EXCLUDE_FROM_ALL)
else()
    message(STATUS "QuantEngine: Fetching Google Benchmark via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_FORCE_WERROR OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)
endif()

add_executable(quantengine_benchmarks
    bench_matching_engine.cpp
)

target_compile_features(quantengine_benchmarks PRIVATE cxx_std_20)

target_include_directories(quantengine_benchmarks
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_options(quantengine_benchmarks PRIVATE ${QUANTENGINE_WARNING_FLAGS})

target_link_libraries(quantengine_benchmarks
    PRIVATE
        quantengine_core
        benchmark::benchmark
)
