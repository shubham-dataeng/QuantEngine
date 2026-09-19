if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/pybind11/CMakeLists.txt")
    message(STATUS "QuantEngine: Using vendored pybind11 from third_party/pybind11")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/pybind11" "${CMAKE_CURRENT_BINARY_DIR}/third_party/pybind11" EXCLUDE_FROM_ALL)
else()
    message(STATUS "QuantEngine: Finding pybind11 package")
    find_package(pybind11 REQUIRED)
endif()

pybind11_add_module(quantengine
    bindings.cpp
)

target_compile_features(quantengine PRIVATE cxx_std_20)

target_include_directories(quantengine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_options(quantengine PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
)

target_link_libraries(quantengine
    PRIVATE
        quantengine_core
)

set_target_properties(quantengine PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/python"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/python"
)
