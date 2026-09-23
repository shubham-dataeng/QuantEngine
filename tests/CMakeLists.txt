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
    unit/test_optimized_order_book.cpp
    fuzz/test_differential_fuzz.cpp
    # Platform layer TDD harnesses (Milestone 3 & M4 through M14)
    unit/test_risk_manager.cpp
    unit/test_event_routing.cpp
    unit/test_sim_gateway.cpp
    unit/test_portfolio.cpp
    unit/test_strategy.cpp
    unit/test_binary_journal.cpp
    unit/test_order_state_machine.cpp
    unit/test_circuit_breaker.cpp
    unit/test_alpaca_integration.cpp
    unit/test_async_journal.cpp
    # Phase 2, 3, 4, 5: L3 replay subsystem tests
    unit/test_l3_messages.cpp
    unit/test_historical_book.cpp
    unit/test_execution_simulation.cpp
    unit/test_replay_experiment.cpp
)

target_compile_features(quantengine_tests PRIVATE cxx_std_20)

# Unlock StandardRiskManagerTest cases now that the implementation exists
target_compile_definitions(quantengine_tests PRIVATE QUANTENGINE_RISK_IMPL)

target_compile_options(quantengine_tests PRIVATE ${QUANTENGINE_WARNING_FLAGS})

target_link_libraries(quantengine_tests
    PRIVATE
        quantengine_core
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(quantengine_tests)

# Python PyTest Suite
if (QUANTENGINE_BUILD_PYTHON)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -m pytest --version
        RESULT_VARIABLE PYTEST_STATUS
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if (PYTEST_STATUS EQUAL 0)
        add_test(
            NAME quantengine_python_tests
            COMMAND ${Python3_EXECUTABLE} -m pytest "${CMAKE_CURRENT_SOURCE_DIR}/python" -v
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
        )
        set_tests_properties(quantengine_python_tests PROPERTIES
            ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/python"
        )
    else()
        message(STATUS "QuantEngine: pytest not found in environment, skipping python test registration")
    endif()
endif()
