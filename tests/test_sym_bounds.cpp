// ===========================================================================
// test_sym_bounds.cpp — per-rule unit tests for the interval-bounds engine.
//
// RED phase: every node is interned with the full range (see the stub note in
// add_expr), so every EXACTNESS assertion below fails on an assertion. The
// soundness-shaped assertions (saturation, vmin <= vmax) pass at red on
// purpose — they are the guardrail for the green phase and for every later
// tightening of the propagation rules.
// ===========================================================================
import std;
import boost.ut;
import atml.core;

namespace ut = boost::ut;

// DECISION: Boost.UT's default console reporter re-prints the whole accumulated
// failure log after every new failure, so a test case with N failing assertions
// emits O(N^2) output — unusable as a red-phase work list. The alternative
// reporter Boost.UT ships prints each failure exactly once, immediately followed
// by its message, which is what lets expected/actual sit on the lines directly
// under the result. This is Boost.UT's documented cfg customization point.
template <class... Ts>
inline auto boost::ut::cfg<boost::ut::override, Ts...> =
    boost::ut::runner<boost::ut::reporter<boost::ut::printer>>{};

namespace {

constexpr atml::Bound kFull{
  std::numeric_limits<std::int64_t>::min(),
  std::numeric_limits<std::int64_t>::max()
};

// --- failure diagnostics ----------------------------------------------------
// Boost.UT prints a failure as a single line, which is unreadable once the
// values are 19-digit int64 endpoints. Starting the message with a newline puts
// expected/actual on their own aligned lines directly beneath the result, so a
// failing run reads as a work list while the engine is being implemented.
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

// Both helpers forward the caller's source_location, so the reported line is
// the assertion's own, not this file's helper. Note this is Boost.UT's OWN
// reflection::source_location: under `import std` the <source_location> feature
// macro is not visible to ut.hpp, so it falls back to its __builtin_FILE /
// __builtin_LINE class rather than aliasing std::source_location.
using SrcLoc = ut::reflection::source_location;

void bound_is(const atml::Bound& got, const atml::Bound& want,
              std::string_view what = {},
              const SrcLoc& loc = SrcLoc::current()) {
  ut::expect(got == want, loc)
      << diff(atml::to_string(want), atml::to_string(got), what);
}

void value_is(std::int64_t got, std::int64_t want,
              std::string_view what = {},
              const SrcLoc& loc = SrcLoc::current()) {
  ut::expect(got == want, loc)
      << diff(std::to_string(want), std::to_string(got), what);
}

// --- fresh variable names ---------------------------------------------------
// The intern table is process-global and re-declaring a name with a different
// interval is a hard error (DECISION §1c), so a test cannot reuse "i" with a
// different range in the next case. fresh() mints a unique name per use.
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
  pool.push_back("v" + std::to_string(pool.size()));
  return pool.back();
}

atml::Sym fresh(std::int64_t lo, std::int64_t hi) {
  return atml::sym(mint(), lo, hi);
}

atml::Sym fresh() {
  return atml::sym(mint());
}

ut::suite sym_bounds_suite = [] {
  using namespace ut;
  using namespace atml;

  "leaves carry their own interval"_test = [] {
    bound_is(bounds(sym_const(5)), Bound{5, 5}, "sym_const(5)");

    for (const std::int64_t c : {-9223372036854775807LL - 1, -7LL, 0LL, 1LL,
                                 9223372036854775807LL}) {
      const Bound b = bounds(sym_const(c));
      const std::string what = "sym_const(" + std::to_string(c) + ")";

      ut::expect(b.vmin == b.vmax)
          << report(field("case", what),
                    field("expected", "a point interval, vmin == vmax"),
                    field("actual", to_string(b)));
      bound_is(b, Bound{c, c}, what);
    }

    bound_is(bounds(fresh()), kFull, "unbounded variable");
    bound_is(bounds(fresh(0, 2)), Bound{0, 2}, "sym(v, 0, 2)");
  };

  "re-interning a bounded variable keeps node identity and bound"_test = [] {
    const auto name = mint();

    const Sym a = sym(name, 0, 2);
    const Sym b = sym(name, 0, 2);

    expect(a == b) << field("case", "identical re-declaration must intern once");
    bound_is(bounds(b), bounds(a), "re-interned bound differs");
    bound_is(bounds(a), Bound{0, 2}, "sym(v, 0, 2) re-interned");
  };

  // DECISION §1c: hard error, not silent re-bind and not keying on
  // {name, lo, hi}. eval() resolves bindings by NAME, so two intervals for one
  // name could both be "satisfied" by a single binding — an unsound bound.
  "conflicting re-declaration is a hard error"_test = [] {
    expect(sym("redecl_a", 0, 4) == sym("redecl_a", 0, 4))
        << field("case", "identical re-declaration must be idempotent");

    expect(throws<std::invalid_argument>([] { (void)sym("redecl_a", 0, 5); }))
        << field("case", "[0,4] then [0,5] must throw");

    (void)sym("redecl_b");                                // declares full range
    expect(throws<std::invalid_argument>([] { (void)sym("redecl_b", 0, 2); }))
        << field("case", "unbounded then [0,2] must throw");

    // lo > hi: an empty interval has no sound representation, and Invalid is
    // Day 2 scope.
    expect(throws<std::invalid_argument>([] { (void)sym("redecl_c", 3, 1); }))
        << field("case", "lo > hi must throw");
    expect(nothrow([] { (void)sym("redecl_c", 3, 3); }))
        << field("case", "a rejected declaration must not register the name");
  };

  "Add: endpoints add"_test = [] {
    bound_is(bounds(fresh(0, 3) + fresh(0, 2)), Bound{0, 5}, "[0,3] + [0,2]");
    bound_is(bounds(fresh(0, 3) + 10), Bound{10, 13}, "[0,3] + 10");
    bound_is(bounds(fresh(-2, 3) + fresh(-1, 1)), Bound{-3, 4}, "[-2,3] + [-1,1]");
  };

  // Sign flips SWAP the endpoints, which is why the rule is min/max over all
  // four endpoint products rather than min(a.vmin*b.vmin, ...) alone.
  "Mul: min/max over the four endpoint products"_test = [] {
    bound_is(bounds(fresh(0, 3) * 4), Bound{0, 12}, "[0,3] * 4");
    bound_is(bounds(fresh(0, 3) * -2), Bound{-6, 0},
             "[0,3] * -2  (endpoint swap)");
    bound_is(bounds(fresh(-2, 3) * fresh(-1, 4)), Bound{-8, 12},
             "[-2,3] * [-1,4]  (corners 2, -8, -3, 12)");
    bound_is(bounds(fresh(0, 3) * 0), Bound{0, 0}, "[0,3] * 0");
  };

  // Floor semantics are already pinned by test_sym.cpp: the quotient rounds
  // toward -inf. With 0 excluded from the divisor interval, floordiv is
  // monotone in each argument separately over the box, so the extrema sit at
  // corners. With 0 included there is no finite bound, so widen to full range.
  "FDiv: corners, and full range when the divisor spans zero"_test = [] {
    bound_is(bounds(floordiv(fresh(0, 7), 2)), Bound{0, 3}, "[0,7] // 2");
    bound_is(bounds(floordiv(fresh(-7, 7), 2)), Bound{-4, 3}, "[-7,7] // 2");
    bound_is(bounds(floordiv(fresh(0, 7), -2)), Bound{-4, 0}, "[0,7] // -2");
    bound_is(bounds(floordiv(fresh(), fresh(-1, 1))), kFull,
             "divisor [-1,1] spans zero");
    bound_is(bounds(floordiv(fresh(0, 7), 1)), Bound{0, 7}, "[0,7] // 1");
  };

  // DECISION §2: for a CONSTANT divisor c != 0, floor-mod lands in [0, c-1]
  // when c > 0 and [c+1, 0] when c < 0 (the result takes the divisor's sign and
  // |r| < |c|). Any other divisor widens to full range. The tighter "operand is
  // already in range" refinement is Day 2.
  //
  // The dividend must be a VARIABLE: mod(sym_const(a), sym_const(b)) is
  // constant-folded by add_expr and never becomes a Mod node.
  "Mod: constant divisor pins the residue interval"_test = [] {
    bound_is(bounds(mod(fresh(), 5)), Bound{0, 4}, "unbounded % 5");
    bound_is(bounds(mod(fresh(), -5)), Bound{-4, 0}, "unbounded % -5");
    bound_is(bounds(mod(fresh(-3, 3), 8)), Bound{0, 7},
             "[-3,3] % 8  (no Day 1 refinement)");
    bound_is(bounds(mod(fresh(), fresh(1, 3))), kFull, "non-constant divisor");
  };

  // These must not trip UBSan: INT64_MIN + INT64_MIN is undefined behaviour,
  // not a wrong number. The green phase hand-rolls saturation with
  // __builtin_add_overflow / __builtin_mul_overflow and clamps.
  "saturation: widening never wraps"_test = [] {
    bound_is(bounds(fresh() + fresh()), kFull, "unbounded + unbounded");
    bound_is(bounds(fresh() * 2), kFull, "unbounded * 2");

    const Bound b = bounds(fresh() + 1);
    value_is(b.vmax, std::numeric_limits<std::int64_t>::max(),
             "unbounded + 1 wrapped vmax");
    value_is(b.vmin, std::numeric_limits<std::int64_t>::min(),
             "unbounded + 1 wrapped vmin");
  };

  "invariant: vmin <= vmax on every node"_test = [] {
    const Sym i = fresh(-2, 3);
    const Sym j = fresh(-1, 4);
    const Sym u = fresh();

    const std::vector<std::pair<std::string_view, Sym>> all{
      {"sym_const(0)", sym_const(0)},   {"sym_const(-7)", sym_const(-7)},
      {"i = [-2,3]", i},                {"j = [-1,4]", j},
      {"u = unbounded", u},             {"i + j", i + j},
      {"i + 10", i + 10},               {"i * j", i * j},
      {"i * -2", i * -2},               {"i * 0", i * 0},
      {"j // 2", floordiv(j, 2)},       {"j // -2", floordiv(j, -2)},
      {"u // i", floordiv(u, i)},       {"u % 5", mod(u, 5)},
      {"u % -5", mod(u, -5)},           {"u % i", mod(u, i)},
      {"(i + j) * (i + 1)", (i + j) * (i + 1)},
      {"(i*4 + j) // 4", floordiv(i * 4 + j, 4)},
    };

    for (const auto& [what, node] : all) {
      const Bound b = bounds(node);
      expect(b.vmin <= b.vmax)
          << report(field("case", what),
                    field("expected", "vmin <= vmax"),
                    field("actual", to_string(b)));
    }
  };

  "deep chain: 1000 increments accumulate"_test = [] {
    Sym s = fresh(0, 0);
    for (int n = 0; n < 1000; n++)
      s = s + 1;

    bound_is(bounds(s), Bound{1000, 1000}, "[0,0] plus 1000 increments");
  };

  // bounds() must be an O(1) stored-field read. A recursive walk over this
  // chain would exhaust the stack rather than fail an assertion. eval() and any
  // pretty-printer legitimately recurse, so neither is called here.
  "deep chain: bounds() reads a stored field, it does not walk"_test = [] {
    Sym s = fresh(0, 0);
    for (int n = 0; n < 100000; n++)
      s = s + 1;

    const Bound b = bounds(s);
    expect(b.vmin <= b.vmax)
        << report(field("case", "[0,0] plus 100000 increments"),
                  field("expected", "vmin <= vmax"),
                  field("actual", to_string(b)));
  };
};

}  // namespace
