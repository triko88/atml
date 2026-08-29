# ===========================================================================
# tests/CMakeLists.txt — unit tests using Boost.UT
#
# This is a subdirectory of the top-level project, so it inherits that
# project's cmake_minimum_required (3.21) — no separate floor needed here.
# ===========================================================================

include(FetchContent)

# Boost.UT — a header-only, macro-free, C++20 micro unit-testing framework.
# (Despite the "Boost" name, it is NOT an official Boost library.)
# FetchContent downloads and configures it at CMake-configure time, so there's
# no system install or package manager involved.
FetchContent_Declare(
    ut
    GIT_REPOSITORY https://github.com/boost-ext/ut.git
    GIT_TAG        v2.3.1          # pin to a release tag — don't track master
    GIT_SHALLOW    TRUE            # grab just that commit, not full history
)
FetchContent_MakeAvailable(ut)

# ut ships a `boost.ut` module interface (include/boost/ut.cppm), but enabling
# it through ut's own CMake (BOOST_UT_DISABLE_MODULE=OFF) clobbers our
# `import std` setup with a stale CMake 3.30 experimental UUID. So build the
# shipped interface ourselves; tests `import boost.ut;` via this target.
add_library(ut_module STATIC)
target_sources(ut_module
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${ut_SOURCE_DIR}/include/boost
        FILES ${ut_SOURCE_DIR}/include/boost/ut.cppm
)
target_compile_features(ut_module PUBLIC cxx_std_23)
add_library(Boost::ut_module ALIAS ut_module)

# One test executable. Add more .cpp files here as your suite grows; UT auto-
# registers every test across all translation units, so you keep one binary.
add_executable(atml_tests
    test_main.cpp
)

# Link the library under test plus the framework.
#  - atml::atml supplies main() (its object is pulled out of the static archive
#    to satisfy the C runtime's reference to main); the Boost.UT runner then
#    executes the registered suites at program exit.
#  - Boost::ut is an INTERFACE target carrying a cxx_std_20 requirement; atml's
#    PUBLIC cxx_std_23 wins, so atml_tests compiles as C++23.
target_link_libraries(atml_tests
    PRIVATE
        atml::atml
        Boost::ut
)

# Register the binary with CTest so `ctest` (run from your build dir) runs it.
add_test(NAME atml_tests COMMAND atml_tests)

# ---------------------------------------------------------------------------
# atml.core tests — one executable per API surface (DType / Sym / Dim).
# Each file does `import atml.core;` (built by the parent as part of the atml
# target) and `import boost.ut;`. As above, atml::atml supplies main() and
# Boost.UT's runner executes the suites at program exit.
# ---------------------------------------------------------------------------
# Helpers shared by the Day 2 Sym tests (failure formatting, the fresh-name
# pool, seed selection). A test-local named module, so it needs its own
# CXX_MODULES file set; PUBLIC linkage to atml::atml because its interface
# mentions atml::Sym and consumers must see the same BMI.
add_library(test_support STATIC)
target_sources(test_support
    PUBLIC
        FILE_SET CXX_MODULES FILES test_support.cppm
)
target_link_libraries(test_support PUBLIC atml::atml)
target_compile_options(test_support PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

foreach(core_test IN ITEMS test_dtype test_sym test_dim
                           test_sym_bounds test_sym_soundness
                           test_sym_simplify test_sym_rewrite_property)
    add_executable(${core_test} ${core_test}.cpp)
    target_link_libraries(${core_test}
        PRIVATE
            atml::atml
            Boost::ut_module
    )
    add_test(NAME ${core_test} COMMAND ${core_test})
endforeach()

# The Day 2 rewrite tests are the only ones that use the shared helper module;
# the Day 1 files keep their own local copies and are deliberately left alone.
foreach(rewrite_test IN ITEMS test_sym_simplify test_sym_rewrite_property)
    target_link_libraries(${rewrite_test} PRIVATE test_support)
endforeach()

# The Phase A bounds tests are new code, so they are held to the same warning
# bar as the library itself. (The older core tests predate this and are left
# alone.) Sanitizers come from atml's PUBLIC flags -- see ATML_SANITIZE in the
# top-level CMakeLists.txt; putting them on the library is what makes the
# bounds propagation code itself instrumented, not just the test TU.
foreach(strict_test IN ITEMS test_sym_bounds test_sym_soundness
                             test_sym_simplify test_sym_rewrite_property)
    target_compile_options(${strict_test} PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic -Werror>
      $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    )
endforeach()

