# ===========================================================================
# CMakeLists.txt — a modern setup for a C++ library shipped as a named module
#
# Project layout:
#   atml/
#     CMakeLists.txt          <- this file
#     src/shape_tracker.cppm  <- module interface unit  (export module atml;)
#     src/shape_tracker.cpp   <- module implementation  (module atml;)
#     src/atml.cpp            <- entry point (provides main())
#     tests/CMakeLists.txt    <- unit tests (Boost.UT, wired in below)
#
# Consumers write `import atml;` — there are no installed headers.
#
# Requires: a generator that scans for C++ modules (Ninja / Visual Studio; the
# Makefiles generator does NOT), plus a compiler that provides the std module
# (GCC 15+, Clang 18+, MSVC). Configure with, e.g.:  cmake -S . -B build -G Ninja
# ===========================================================================

# 3.30 is the floor for stable `import std;` support (CMAKE_CXX_MODULE_STD).
cmake_minimum_required(VERSION 3.30)

# `import std;` is still behind an experimental gate: the UUID unlocks it and
# must be set BEFORE project() (i.e. before the CXX toolchain is probed). This
# value is CMake-version-specific — it matches the CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
# gate in CMake 4.3. Bumping CMake may require updating it.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

project(atml
    VERSION      0.1.0
    DESCRIPTION  "Apan Trikha's ML stack (ATML)"
    LANGUAGES    CXX
)

# Build the standard library as a module so `import std;` works. Must be set
# before any target that imports std is created.
set(CMAKE_CXX_MODULE_STD ON)

# ---------------------------------------------------------------------------
# 1. The library target
# ---------------------------------------------------------------------------
# Static by default; consumers can flip to shared via -DBUILD_SHARED_LIBS=ON.
#  - shape_tracker.cpp is the module implementation unit; atml.cpp provides main().
#    Both are scanned for module dependencies because the target owns a
#    CXX_MODULES file set (below).
add_library(atml
    src/atml.cpp
    src/shape_tracker.cpp
)

# The module interface unit, exposed as a PUBLIC module file set so in-tree
# consumers (the tests) can `import atml;` against the BMI CMake builds for it.
target_sources(atml
    PUBLIC
        FILE_SET CXX_MODULES FILES
            src/shape_tracker.cppm
)

# A namespaced alias so the library links the same way whether pulled in via
# add_subdirectory() or find_package() — always "atml::atml". The tests below
# rely on this alias, so it must be defined before add_subdirectory(tests).
add_library(atml::atml ALIAS atml)

# ---------------------------------------------------------------------------
# 2. Language standard
# ---------------------------------------------------------------------------
# PUBLIC: code that links atml is also compiled as (at least) C++23.
target_compile_features(atml PUBLIC cxx_std_23)

# PRIVATE: the compiler options are exclusively for atml.
target_compile_options(atml PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# ---------------------------------------------------------------------------
# 3. Tests
# ---------------------------------------------------------------------------
# Register tests with CTest so `ctest` (run from the build dir) runs them.
# Toggle off with -DATML_BUILD_TESTS=OFF.
option(ATML_BUILD_TESTS "Build the unit tests" ON)
if(ATML_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
