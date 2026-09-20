# cmake/QuantEngineCompileOptions.cmake
#
# Reusable function that applies the project-wide compile guardrails to any
# CMake target. All Phase 1 platform targets (platform_core, market_data, etc.)
# MUST call this function — no target may weaken these flags.
#
# Usage:
#   quantengine_apply_compile_options(TARGET my_target [SCOPE PUBLIC|PRIVATE|INTERFACE])
#
# Scope defaults to PRIVATE (correct for executables and static/shared libs).
# Use INTERFACE for header-only targets. Use PUBLIC for targets that export
# compiler options to consumers (e.g. quantengine_core which exports -fsanitize).

include_guard(GLOBAL)

function(quantengine_apply_compile_options TARGET)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "SCOPE" "")
    if (NOT ARG_SCOPE)
        set(ARG_SCOPE PRIVATE)
    endif()

    # -------------------------------------------------------------------------
    # Core warning flags — same set as on quantengine_core. Junior agents must
    # NOT weaken these or add target_compile_options that disable any of them.
    # -------------------------------------------------------------------------
    set(_WARN_FLAGS
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
        # Additional flags for the platform layer (stricter than the engine):
        -Wdouble-promotion          # float-to-double implicit promotion
        -Wformat=2                  # printf/scanf format string safety
        -Wnull-dereference          # GCC: warn on obvious null deref paths
        -Wold-style-cast            # any (T)expr cast is an error
        -Wmisleading-indentation    # dangling-else-style indentation bugs
        -Wlogical-op                # suspicious logical operator uses (GCC)
    )

    if (QUANTENGINE_TREAT_WARNINGS_AS_ERRORS)
        list(APPEND _WARN_FLAGS -Werror)
    endif()

    target_compile_options(${TARGET} ${ARG_SCOPE} ${_WARN_FLAGS})

    # -------------------------------------------------------------------------
    # C++20 standard — mandatory for all platform targets
    # -------------------------------------------------------------------------
    target_compile_features(${TARGET} ${ARG_SCOPE} cxx_std_20)

    # -------------------------------------------------------------------------
    # Sanitizers — propagated from the root option, same as quantengine_core.
    # For new shared-library targets, scope must be PUBLIC so the linker flags
    # propagate to the test executable that links against them.
    # -------------------------------------------------------------------------
    if (QUANTENGINE_ENABLE_SANITIZERS)
        set(_SAN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(${TARGET} ${ARG_SCOPE} ${_SAN_FLAGS})
        target_link_options(${TARGET}    ${ARG_SCOPE} ${_SAN_FLAGS})
    endif()

    # -------------------------------------------------------------------------
    # clang-tidy integration — hooked into the build when clang-tidy is found.
    # The .clang-tidy file at the repo root is discovered automatically via
    # CMAKE_EXPORT_COMPILE_COMMANDS=ON (already set in all CMakePresets).
    # CI must set QUANTENGINE_ENABLE_CLANG_TIDY=ON.
    # -------------------------------------------------------------------------
    if (QUANTENGINE_ENABLE_CLANG_TIDY)
        find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-18 clang-tidy-17)
        if (CLANG_TIDY_EXE)
            message(STATUS "QuantEngine: clang-tidy enabled for target '${TARGET}' (${CLANG_TIDY_EXE})")
            set_target_properties(${TARGET} PROPERTIES
                CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--warnings-as-errors=*"
            )
        else()
            message(WARNING "QUANTENGINE_ENABLE_CLANG_TIDY=ON but clang-tidy not found; skipping.")
        endif()
    endif()
endfunction()

# -------------------------------------------------------------------------
# Convenience wrapper: create a static library target and apply guardrails.
# Used for every Phase 1 sub-library (market_data, execution, risk, ...).
#
# Usage:
#   quantengine_add_library(
#       NAME    quantengine_market
#       SOURCES src/market/CsvFeed.cpp src/market/ReplayFeed.cpp
#       HEADERS include/quantengine/market/IMarketDataFeed.hpp ...
#   )
# -------------------------------------------------------------------------
function(quantengine_add_library)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME" "SOURCES;HEADERS;DEPS")

    if (ARG_SOURCES)
        add_library(${ARG_NAME} STATIC ${ARG_SOURCES})
    else()
        # Header-only: use INTERFACE library
        add_library(${ARG_NAME} INTERFACE)
        target_include_directories(${ARG_NAME} INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        )
        if (ARG_DEPS)
            target_link_libraries(${ARG_NAME} INTERFACE ${ARG_DEPS})
        endif()
        return()
    endif()

    target_include_directories(${ARG_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if (ARG_DEPS)
        target_link_libraries(${ARG_NAME} PUBLIC ${ARG_DEPS})
    endif()

    quantengine_apply_compile_options(${ARG_NAME} SCOPE PRIVATE)
    set_target_properties(${ARG_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
