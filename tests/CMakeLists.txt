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

# One test executable. Add more .cpp files here as your suite grows; UT auto-
# registers every test across all translation units, so you keep one binary.
add_executable(atml_tests
    test_shape_tracker.cpp
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
