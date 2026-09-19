cmake_minimum_required(VERSION 3.28)
project(QuantEngine
    VERSION 0.1.0
    DESCRIPTION "Deterministic C++20 Limit Order Matching Engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(QUANTENGINE_BUILD_TESTS "Build QuantEngine unit and integration test suite" ON)
option(QUANTENGINE_BUILD_BENCHMARKS "Build QuantEngine benchmark suite" ON)
option(QUANTENGINE_BUILD_PYTHON "Build QuantEngine Python bindings" ON)
option(QUANTENGINE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(QUANTENGINE_TREAT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# Common compiler warning flags
set(QUANTENGINE_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
)

if (QUANTENGINE_TREAT_WARNINGS_AS_ERRORS)
    list(APPEND QUANTENGINE_WARNING_FLAGS -Werror)
endif()

# Core Library Target
add_library(quantengine_core
    src/core/version.cpp
    src/core/types.cpp
    src/core/trade.cpp
    src/core/order.cpp
    src/reference/reference_order_book.cpp
    src/optimized/optimized_order_book.cpp
    src/engine/matching_engine.cpp
    src/engine/canonical_state.cpp
    src/engine/invariants.cpp
    src/engine/replay.cpp
)

target_compile_features(quantengine_core PUBLIC cxx_std_20)

target_include_directories(quantengine_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(quantengine_core PRIVATE ${QUANTENGINE_WARNING_FLAGS})
set_target_properties(quantengine_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Sanitizers setup
if (QUANTENGINE_ENABLE_SANITIZERS)
    message(STATUS "QuantEngine: Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
    set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(quantengine_core PUBLIC ${SANITIZER_FLAGS})
    target_link_options(quantengine_core PUBLIC ${SANITIZER_FLAGS})
endif()

# Testing
if (QUANTENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Benchmarks
if (QUANTENGINE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Python Bindings
if (QUANTENGINE_BUILD_PYTHON)
    add_subdirectory(bindings/python)
endif()

