// ===========================================================================
// test_support.cppm — module `test_support`: helpers shared by the Day 2 Sym
// test binaries. Test-local, NOT part of the `atml` library.
//
// Deliberately does NOT depend on Boost.UT. Two things stay per-TU:
//
//   * the `cfg<boost::ut::override>` reporter specialization — it is a
//     variable-template specialization the runner picks up per translation
//     unit;
//   * assertion wrappers such as bound_is / value_is — they default a
//     `ut::reflection::source_location::current()` argument, which must be
//     evaluated at the CALLER's line to report a usable file:line.
// ===========================================================================
export module test_support;

import std;
import atml.core;

export namespace atml_test {

// --- failure diagnostics ----------------------------------------------------
// Boost.UT prints a failure as a single line, which is unreadable once the
// values are 19-digit int64 endpoints or a nested expression dump. Starting the
// message with a newline puts each field on its own aligned line directly
// beneath the result, so a failing run reads as a work list.
std::string field(std::string_view key, std::string_view value) {
  std::string label{key};
  label.resize(10, ' ');
  return "\n      " + label + std::string{value};
}

// Boost.UT inserts a space before every `<<` operand, which would land at the
// end of the preceding field line. Folding the fields into ONE operand keeps
// the block clean.
template <class... Ts>
std::string report(const Ts&... fields) {
  return (std::string{} + ... + fields);
}

std::string diff(std::string_view want, std::string_view got,
                 std::string_view what) {
  return (what.empty() ? std::string{} : field("case", what))
       + field("expected", want)
       + field("actual", got);
}

// --- fresh variable names ---------------------------------------------------
// The intern table is process-global and re-declaring a name with a different
// interval is a hard error (PHASE_A_DAY1.md §"Variable re-declaration"), so a
// test cannot reuse "j" with a different range in the next case. mint() hands
// out a unique name per call.
//
// The pool has static storage duration and is never popped: intern::vars and
// intern::Node store a std::string_view, so the backing characters must outlive
// the table. std::deque is used because push_back never invalidates references
// to existing elements.
std::deque<std::string>& name_pool() {
  static std::deque<std::string> pool;
  return pool;
}

std::string_view mint() {
  auto& pool = name_pool();
  pool.push_back("t" + std::to_string(pool.size()));
  return pool.back();
}

atml::Sym fresh(std::int64_t lo, std::int64_t hi) {
  return atml::sym(mint(), lo, hi);
}

atml::Sym fresh() {
  return atml::sym(mint());
}

// --- reproducible seeds -----------------------------------------------------
// Every fuzz failure prints `ATML_TEST_SEED=<n>`; exporting that in the
// environment replays the exact case.
std::uint64_t pick_seed(std::uint64_t fallback) {
  if (const char* env = std::getenv("ATML_TEST_SEED"))
    return std::strtoull(env, nullptr, 10);
  return fallback;
}

}  // namespace atml_test
