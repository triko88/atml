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
foreach(core_test IN ITEMS test_dtype test_sym test_dim)
    add_executable(${core_test} ${core_test}.cpp)
    target_link_libraries(${core_test}
        PRIVATE
            atml::atml
            Boost::ut_module
    )
    add_test(NAME ${core_test} COMMAND ${core_test})
endforeach()

# ---------------------------------------------------------------------------
# atml.view tests (Day 2) — the movement ops checked against a REAL dense
# oracle. The oracle lives in its own module (test_oracle), built here as a
# static library so all three view test executables can import it. It is
# test-local: NOT part of the atml library under test.
# ---------------------------------------------------------------------------
add_library(test_oracle_lib STATIC)
target_sources(test_oracle_lib
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES oracle.cppm
)
target_compile_features(test_oracle_lib PUBLIC cxx_std_23)
add_library(test::oracle ALIAS test_oracle_lib)

foreach(view_test IN ITEMS test_view_ops test_view_property test_view_symbolic)
    add_executable(${view_test} ${view_test}.cpp)
    target_link_libraries(${view_test}
        PRIVATE
            atml::atml
            Boost::ut_module
            test::oracle
    )
    add_test(NAME ${view_test} COMMAND ${view_test})
endforeach()
