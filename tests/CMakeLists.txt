# Check if local GoogleTest checkout exists in third_party
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/googletest/CMakeLists.txt")
    message(STATUS "QuantEngine: Using vendored GoogleTest from third_party/googletest")
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../third_party/googletest" "${CMAKE_CURRENT_BINARY_DIR}/third_party/googletest" EXCLUDE_FROM_ALL)
else()
    message(STATUS "QuantEngine: Fetching GoogleTest via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# Unit tests executable
add_executable(quantengine_tests
    unit/test_smoke.cpp
    unit/test_types.cpp
    unit/test_order.cpp
    unit/test_trade.cpp
    unit/test_reference_order_book.cpp
    unit/test_matching_engine.cpp
    unit/test_replay_and_invariants.cpp
)

target_compile_features(quantengine_tests PRIVATE cxx_std_20)

target_compile_options(quantengine_tests PRIVATE ${QUANTENGINE_WARNING_FLAGS})

target_link_libraries(quantengine_tests
    PRIVATE
        quantengine_core
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(quantengine_tests)
