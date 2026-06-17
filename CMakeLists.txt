# ===========================================================================
# CMakeLists.txt — a standard, modern setup for a C++ library (no dependencies)
#
# Project layout:
#   atml/
#     CMakeLists.txt          <- this file
#     include/...             <- public headers (currently empty)
#     src/atml.cpp            <- implementation / entry point
#     tests/CMakeLists.txt    <- unit tests (Boost.UT, wired in below)
# ===========================================================================

# 3.21 is a sane modern floor and also satisfies Boost.UT's own requirement.
cmake_minimum_required(VERSION 3.21)

project(atml
    VERSION      0.1.0
    DESCRIPTION  "Apan Trikha's ML stack (ATML)"
    LANGUAGES    CXX
)

# ---------------------------------------------------------------------------
# 1. The library target
# ---------------------------------------------------------------------------
# Static by default; consumers can flip to shared via -DBUILD_SHARED_LIBS=ON.
add_library(atml
    src/atml.cpp
    src/shape_tracker.cpp
)

# A namespaced alias so the library links the same way whether pulled in via
# add_subdirectory() or find_package() — always "atml::atml". The tests below
# rely on this alias, so it must be defined before add_subdirectory(tests).
add_library(atml::atml ALIAS atml)

# ---------------------------------------------------------------------------
# 2. Include directories
# ---------------------------------------------------------------------------
# PUBLIC: needed to build atml AND propagated to anything that links it.
target_include_directories(atml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ---------------------------------------------------------------------------
# 3. Language standard
# ---------------------------------------------------------------------------
# PUBLIC: code that links atml is also compiled as (at least) C++23.
target_compile_features(atml PUBLIC cxx_std_23)

# PRIVATE: the compiler options are exclusively for atml.
target_compile_options(atml PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# ---------------------------------------------------------------------------
# 4. Tests
# ---------------------------------------------------------------------------
# Register tests with CTest so `ctest` (run from the build dir) runs them.
# Toggle off with -DATML_BUILD_TESTS=OFF.
option(ATML_BUILD_TESTS "Build the unit tests" ON)
if(ATML_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
