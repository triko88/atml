# ===========================================================================
# CMakeLists.txt — a modern setup for a C++ library shipped as a named module
#
# Project layout:
#   atml/
#     CMakeLists.txt          <- this file
#     src/atml_dim.cppm       <- primary interface of atml.core (Dim API)
#     src/atml_sym.cppm       <- atml.core:sym partition (Sym, Bindings, Bound)
#     src/atml_rewrite.cppm   <- atml.core:rewrite partition (simplify, to_string)
#     src/atml_dtype.cppm     <- atml.core:dtype partition (DType, f16)
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
# gate in CMake 4.4. Bumping CMake may require updating it.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")

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
#    Both are scanned for module dependencies because the target owns a
#    CXX_MODULES file set (below).
add_library(atml
    src/atml.cpp
)

# The module interface unit, exposed as a PUBLIC module file set so in-tree
# consumers (the tests) can `import atml;` against the BMI CMake builds for it.
target_sources(atml
    PUBLIC
        FILE_SET CXX_MODULES FILES
            src/atml_core.cppm
            src/atml_dtype.cppm
            src/atml_sym.cppm
            src/atml_rewrite.cppm
            src/atml_dim.cppm
)

# A namespaced alias so the library links the same way whether pulled in via
# add_subdirectory() or find_package() — always "atml::atml". The tests below
# rely on this alias, so it must be defined before add_subdirectory(tests).
add_library(atml::atml ALIAS atml)

# ---------------------------------------------------------------------------
# 2. Language standard
# ---------------------------------------------------------------------------
# PUBLIC: code that links atml is also compiled as (at least) C++26.
target_compile_features(atml PUBLIC cxx_std_26)

# PRIVATE: the compiler options are exclusively for atml.
target_compile_options(atml PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# ---------------------------------------------------------------------------
# 2b. Sanitizers
# ---------------------------------------------------------------------------
# ASan + UBSan, PUBLIC so everything that links atml inherits both the compile
# and the link flags.
#
# These MUST sit on the library, not just on the test executables: the bounds
# engine lives in the atml.core:sym module interface, which is compiled into
# libatml.a by this target. Test-target-only flags would leave the code under
# test uninstrumented, and the whole point is that a signed-overflow slip in the
# saturating interval arithmetic becomes a hard failure instead of a silently
# wrong -- and therefore unsound -- bound.
#
# Flip off with -DATML_SANITIZE=OFF for release builds, or if the `std` module
# and ASan ever disagree under a given toolchain.
option(ATML_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" ON)
if(ATML_SANITIZE)
    target_compile_options(atml PUBLIC
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fsanitize=undefined,address -fno-omit-frame-pointer>
    )
    target_link_options(atml PUBLIC
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fsanitize=undefined,address>
    )
endif()

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
