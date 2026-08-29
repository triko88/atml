// ===========================================================================
// test_sym_soundness.cpp — differential fuzz for the interval-bounds engine.
//
// The reference is `eval`, which is already trusted and tested (test_sym.cpp).
// There is deliberately NO second bounds implementation here: an oracle that
// re-derived intervals would just be the same algorithm written twice, and
// would agree with a wrong engine. Every claim is checked against actually
// observed evaluations instead.
//
// TWO SEPARATE CORPORA, and the split is the whole point:
//
//   A. SOUNDNESS  — arbitrary expressions, variables may repeat. Asserts only
//      containment: vmin <= eval <= vmax. This PASSES at red, because the stub
//      returns the full range. It exists to guard the green phase and every
//      later tightening.
//
//   B. EXACTNESS  — each variable occurs AT MOST ONCE. Under that restriction
//      interval arithmetic over {+, *, //} is exact, so the computed bound must
//      equal the observed min/max. This is what FAILS at red.
//
// DAY 2: corpus A now also emits VARIABLE divisors, including ones whose
// interval straddles zero. Day 1's generator only ever produced non-zero
// constant divisors, which is exactly why it never constructed the input that
// exposes fdiv_bounds' missing zero-spanning carve-out — see
// tests/test_sym_bounds.cpp §"FDiv: a divisor interval containing zero". This
// makes corpus A FAIL at red, on a real live unsoundness rather than on a
// missing optimization.
//
// Corpus B keeps constant divisors on purpose: with a variable divisor the
// corners are no longer the extrema, so exactness would fail legitimately and
// say nothing about the engine.
//
// Exactness must NOT be asserted on corpus A. With repeated variables the
// dependency problem makes interval arithmetic legitimately loose: over
// x in [0,3], `x - x` yields [-3,3] rather than [0,0], because the two
// occurrences are treated as independent. Corpus B avoids that by construction,
// and its endpoints stay achievable by induction: +, * and zero-free // are
// each monotone in every argument separately over the box, so the extrema sit
// at corners, and every corner is realised by some binding.
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

constexpr int kIterations = 2000;
constexpr std::int64_t kMaxPoints = 4096;      // enumeration cap (corpus A)
constexpr std::int64_t kMulBound = std::int64_t{1} << 40;
constexpr std::int64_t kLeafMag = 8;

constexpr std::int64_t iabs(std::int64_t v) { return v < 0 ? -v : v; }

// --- fresh variable names ---------------------------------------------------
// Re-declaring a name with a different interval is a hard error (DECISION §1c)
// and the intern table is process-global, so every iteration mints brand-new
// names. The pool is static and never popped: intern stores a string_view into
// it, and std::deque keeps references to existing elements valid.
std::deque<std::string>& name_pool() {
  static std::deque<std::string> pool;
  return pool;
}

std::string_view mint() {
  auto& pool = name_pool();
  pool.push_back("f" + std::to_string(pool.size()));
  return pool.back();
}

// --- the declared box -------------------------------------------------------
struct Var {
  std::string_view name;
  std::int64_t lo;
  std::int64_t hi;
};

struct VarSet {
  std::vector<Var> vars;
  atml::Bindings binds;
  std::vector<atml::Bindings::iterator> slots;  // parallel to `vars`
  std::int64_t box = 1;                         // number of points in the box
};

VarSet make_vars(std::mt19937_64& rng, int nlo, int nhi, std::int64_t box_cap) {
  VarSet vs;
  const int n = std::uniform_int_distribution<int>(nlo, nhi)(rng);

  for (int a = 0; a < n; a++) {
    const std::int64_t extent = std::uniform_int_distribution<std::int64_t>(1, 8)(rng);
    if (vs.box * extent > box_cap)
      break;

    // Some ranges start below zero, so sign handling in Mul/FDiv is exercised.
    const std::int64_t lo = std::uniform_int_distribution<std::int64_t>(-4, 4)(rng);
    vs.vars.push_back({mint(), lo, lo + extent - 1});
    vs.box *= extent;
  }

  if (vs.vars.empty())                     // never generate a variable-free box
    vs.vars.push_back({mint(), 0, 3}), vs.box = 4;

  for (const Var& v : vs.vars)
    vs.slots.push_back(vs.binds.emplace(std::string{v.name}, v.lo).first);

  return vs;
}

bool next_point(std::vector<std::int64_t>& pt, const std::vector<Var>& vars) {
  for (std::size_t a = vars.size(); a-- > 0;) {
    if (pt[a] < vars[a].hi) {
      pt[a]++;
      return true;
    }
    pt[a] = vars[a].lo;
  }
  return false;
}

// Walks the declared box, writing each point into `vs.binds` before invoking
// `f`. Exhaustive when the box fits under `cap`, uniformly sampled otherwise.
// Corpus B always fits, which is required: sampling would understate the
// observed range and turn exactness into a false failure.
template <class F>
void for_each_point(VarSet& vs, std::mt19937_64& rng, std::int64_t cap, F&& f) {
  std::vector<std::int64_t> pt;
  for (const Var& v : vs.vars)
    pt.push_back(v.lo);

  const auto publish = [&] {
    for (std::size_t a = 0; a < pt.size(); a++)
      vs.slots[a]->second = pt[a];
    f(pt);
  };

  if (vs.box <= cap) {
    do {
      publish();
    } while (next_point(pt, vs.vars));
    return;
  }

  for (std::int64_t n = 0; n < cap; n++) {
    for (std::size_t a = 0; a < pt.size(); a++)
      pt[a] = std::uniform_int_distribution<std::int64_t>(
          vs.vars[a].lo, vs.vars[a].hi)(rng);
    publish();
  }
}

// Failure diagnostics: a leading newline drops each field onto its own aligned
// line beneath the FAILED result, so expected/actual are directly comparable
// rather than buried in a single long line.
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

std::string point_dump(const VarSet& vs, const std::vector<std::int64_t>& pt) {
  std::string s = "{";
  for (std::size_t a = 0; a < vs.vars.size(); a++)
    s += std::string{vs.vars[a].name} + "=" + std::to_string(pt[a])
       + (a + 1 < vs.vars.size() ? ", " : "");
  return s + "}";
}

// --- the generator ----------------------------------------------------------
// Each node carries a readable dump and a conservative magnitude bound, both
// folded in parallel as the tree is built (never by re-walking the Sym). The
// magnitude bound is what keeps `eval` itself free of overflow UB: `*` degrades
// to `+` whenever the product could leave +/-2^40.
struct GenNode {
  atml::Sym expr;
  std::string dump;
  std::int64_t maxabs;

  // Divisor sub-expressions that must be non-zero for a binding to be
  // meaningful. Only variable divisors land here: constant divisors are
  // non-zero by construction. Merged upward as the tree is built.
  std::vector<atml::Sym> guards;
};

std::vector<atml::Sym> merge(std::vector<atml::Sym> a,
                             const std::vector<atml::Sym>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

struct Gen {
  std::mt19937_64& rng;
  const std::vector<Var>& vars;
  std::vector<std::size_t> unused;  // remaining var indices, no-repeat mode
  bool allow_repeat;
  bool allow_mod;
  bool allow_var_divisor;   // corpus A only -- see the header note
  int min_depth;
  int max_depth;
};

std::int64_t draw(Gen& g, std::int64_t lo, std::int64_t hi) {
  return std::uniform_int_distribution<std::int64_t>(lo, hi)(g.rng);
}

std::int64_t draw_divisor(Gen& g) {
  const std::int64_t d = draw(g, 1, kLeafMag);
  return draw(g, 0, 1) == 1 ? d : -d;
}

// A divisor is either a non-zero constant (Day 1) or, in corpus A, one of the
// declared variables. Reusing a declared variable is what keeps this simple:
// make_vars already draws lo uniformly from [-4,4] with extent 1..8, so
// zero-spanning divisor intervals arise naturally, and the variable is already
// present in vs.binds so no extra binding machinery is needed.
struct Divisor {
  atml::Sym expr;
  std::string dump;
  std::int64_t maxabs;
  bool needs_guard;
};

Divisor pick_divisor(Gen& g) {
  if (g.allow_var_divisor and draw(g, 0, 1) == 1) {
    const std::size_t idx = static_cast<std::size_t>(
        draw(g, 0, static_cast<std::int64_t>(g.vars.size()) - 1));
    const Var& v = g.vars[idx];
    return {atml::sym(v.name, v.lo, v.hi), std::string{v.name},
            std::max(iabs(v.lo), iabs(v.hi)), true};
  }

  const std::int64_t d = draw_divisor(g);
  return {atml::sym_const(d), std::to_string(d), iabs(d), false};
}

GenNode const_leaf(Gen& g) {
  const std::int64_t v = draw(g, -kLeafMag, kLeafMag);
  return {atml::sym_const(v), std::to_string(v), iabs(v), {}};
}

GenNode var_leaf(Gen& g) {
  std::size_t idx = 0;

  if (g.allow_repeat) {
    idx = static_cast<std::size_t>(
        draw(g, 0, static_cast<std::int64_t>(g.vars.size()) - 1));
  } else {
    if (g.unused.empty())        // out of unrepeated variables: use a constant
      return const_leaf(g);
    const std::size_t k = static_cast<std::size_t>(
        draw(g, 0, static_cast<std::int64_t>(g.unused.size()) - 1));
    idx = g.unused[k];
    g.unused.erase(g.unused.begin() + static_cast<std::ptrdiff_t>(k));
  }

  const Var& v = g.vars[idx];
  return {atml::sym(v.name, v.lo, v.hi), std::string{v.name},
          std::max(iabs(v.lo), iabs(v.hi)), {}};
}

GenNode gen(Gen& g, int depth) {
  // Below min_depth an operator is forced, so the corpus is actually made of
  // nested trees rather than the occasional bare leaf; at max_depth a leaf is
  // forced, so it terminates.
  const bool leaf_only = depth >= g.max_depth;
  const bool op_only = depth < g.min_depth;
  const int lo = op_only and not leaf_only ? 2 : 0;
  const int top = leaf_only ? 1 : (g.allow_mod ? 5 : 4);

  switch (static_cast<int>(draw(g, lo, top))) {
    case 0:
      return const_leaf(g);

    case 1:
      return var_leaf(g);

    case 2: {  // Add
      const GenNode l = gen(g, depth + 1);
      const GenNode r = gen(g, depth + 1);
      return {l.expr + r.expr, "(" + l.dump + " + " + r.dump + ")",
              l.maxabs + r.maxabs, merge(l.guards, r.guards)};
    }

    case 3: {  // Mul, degraded to Add when the product could overflow the oracle
      const GenNode l = gen(g, depth + 1);
      const GenNode r = gen(g, depth + 1);

      if (l.maxabs != 0 and r.maxabs > kMulBound / l.maxabs)
        return {l.expr + r.expr, "(" + l.dump + " + " + r.dump + ")",
                l.maxabs + r.maxabs, merge(l.guards, r.guards)};

      return {l.expr * r.expr, "(" + l.dump + " * " + r.dump + ")",
              l.maxabs * r.maxabs, merge(l.guards, r.guards)};
    }

    case 4: {  // FDiv by a nonzero constant, or (corpus A) by a variable
      const GenNode l = gen(g, depth + 1);
      const Divisor d = pick_divisor(g);

      std::vector<atml::Sym> guards = l.guards;
      if (d.needs_guard)
        guards.push_back(d.expr);

      // |a // d| <= |a| + 1 for |d| >= 1 (e.g. -1 // 2 == -1); bindings where
      // the divisor is 0 are skipped, never evaluated.
      return {floordiv(l.expr, d.expr),
              "fdiv(" + l.dump + ", " + d.dump + ")",
              l.maxabs + 1, std::move(guards)};
    }

    default: {  // Mod, corpus A only
      const GenNode l = gen(g, depth + 1);
      const Divisor d = pick_divisor(g);

      std::vector<atml::Sym> guards = l.guards;
      if (d.needs_guard)
        guards.push_back(d.expr);

      // |a % d| <= |d| - 1, so the widest divisor magnitude bounds it.
      return {mod(l.expr, d.expr), "mod(" + l.dump + ", " + d.dump + ")",
              std::max<std::int64_t>(1, d.maxabs), std::move(guards)};
    }
  }
}

std::uint64_t pick_seed() {
  if (const char* env = std::getenv("ATML_TEST_SEED"))
    return std::strtoull(env, nullptr, 10);
  return 0xb0d5c0deull;
}

ut::suite sym_soundness_suite = [] {
  using namespace ut;
  using namespace atml;

  // Corpus A. Passes trivially against the full-range stub -- that is expected
  // and intended. It is the assertion that must never regress once the engine
  // starts tightening.
  "soundness: every evaluation lies inside the computed bound"_test = [] {
    const std::uint64_t seed = pick_seed();
    std::mt19937_64 rng(seed);

    int done = 0;
    bool bailed = false;
    std::int64_t checked = 0;

    for (int iter = 0; iter < kIterations; iter++) {
      VarSet vs = make_vars(rng, 3, 5, std::int64_t{1} << 20);
      Gen g{rng, vs.vars, {}, true, true, true, 2,
            static_cast<int>(std::uniform_int_distribution<int>(3, 6)(rng))};

      const GenNode n = gen(g, 0);
      const Bound b = bounds(n.expr);

      bool violated = false;
      std::int64_t witness_val = 0;
      std::string witness_pt;

      for_each_point(vs, rng, kMaxPoints,
        [&](const std::vector<std::int64_t>& pt) {
          if (violated)
            return;

          // A divisor of 0 is a fault, not a value: sat_fdiv returns a
          // saturated sentinel that no legal evaluation can produce, so
          // asserting containment on it would be meaningless.
          for (const Sym& guard : n.guards)
            if (eval(guard, vs.binds) == 0)
              return;

          checked++;
          const std::int64_t got = eval(n.expr, vs.binds);
          if (got < b.vmin or got > b.vmax) {
            violated = true;
            witness_val = got;
            witness_pt = point_dump(vs, pt);
          }
        });

      if (violated) {
        expect(false)
            << report(field("case", "an evaluation escaped the bound"),
                      field("expr", n.dump),
                      field("expected", "a value inside " + to_string(b)),
                      field("actual", std::to_string(witness_val)),
                      field("binding", witness_pt),
                      field("iteration", std::to_string(iter)),
                      field("replay", "ATML_TEST_SEED=" + std::to_string(seed)));
        bailed = true;
        break;   // one report is enough to reproduce
      }

      done++;
    }

    // Without these the suite would report zero assertions on a clean run and
    // read as vacuously green. Skipped after a bail-out, where the failure
    // above is already the report.
    if (not bailed) {
      expect(done == kIterations)
          << report(field("case", "corpus did not run to completion"),
                    field("expected", std::to_string(kIterations) + " expressions"),
                    field("actual", std::to_string(done) + " expressions"));
      expect(checked > kIterations)
          << report(field("case", "corpus evaluated too few bindings to be meaningful"),
                    field("expected", "> " + std::to_string(kIterations) + " bindings"),
                    field("actual", std::to_string(checked) + " bindings"));
    }
  };

  // Corpus B. Each variable occurs at most once, Mod is excluded (its Day 1
  // rule is deliberately loose), and the box is always enumerated exhaustively.
  // This is the red-phase failure.
  "exactness: single-occurrence bounds match the observed range"_test = [] {
    const std::uint64_t seed = pick_seed() ^ 0x5ea1edull;
    std::mt19937_64 rng(seed);

    int done = 0;
    bool bailed = false;

    for (int iter = 0; iter < kIterations; iter++) {
      VarSet vs = make_vars(rng, 2, 4, kMaxPoints);

      std::vector<std::size_t> unused(vs.vars.size());
      std::iota(unused.begin(), unused.end(), std::size_t{0});

      Gen g{rng, vs.vars, unused, false, false, false, 2,
            static_cast<int>(std::uniform_int_distribution<int>(3, 6)(rng))};

      const GenNode n = gen(g, 0);
      const Bound b = bounds(n.expr);

      std::int64_t seen_min = std::numeric_limits<std::int64_t>::max();
      std::int64_t seen_max = std::numeric_limits<std::int64_t>::min();
      std::string min_pt;
      std::string max_pt;

      for_each_point(vs, rng, kMaxPoints,
        [&](const std::vector<std::int64_t>& pt) {
          const std::int64_t got = eval(n.expr, vs.binds);
          if (got < seen_min) {
            seen_min = got;
            min_pt = point_dump(vs, pt);
          }
          if (got > seen_max) {
            seen_max = got;
            max_pt = point_dump(vs, pt);
          }
        });

      if (b.vmin != seen_min or b.vmax != seen_max) {
        expect(false)
            << report(field("case", "not exact on a single-occurrence expr"),
                      field("expr", n.dump),
                      field("expected", to_string(Bound{seen_min, seen_max})
                                        + "   observed over the whole box"),
                      field("actual", to_string(b)
                                      + "   returned by bounds()"),
                      field("min at", min_pt),
                      field("max at", max_pt),
                      field("iteration", std::to_string(iter)),
                      field("replay", "ATML_TEST_SEED=" + std::to_string(pick_seed())));
        bailed = true;
        break;   // one report is enough to reproduce
      }

      done++;
    }

    if (not bailed)
      expect(done == kIterations)
          << report(field("case", "corpus did not run to completion"),
                    field("expected", std::to_string(kIterations) + " expressions"),
                    field("actual", std::to_string(done) + " expressions"));
  };

  // Hash-consing must survive the extra Bound field: the same construction
  // sequence yields the same node, hence the same bound.
  "determinism: rebuilding an expression reuses the node and the bound"_test = [] {
    const auto name = mint();

    const Sym a = floordiv(sym(name, 0, 7) * 4 + sym_const(3), 4);
    const Sym b = floordiv(sym(name, 0, 7) * 4 + sym_const(3), 4);

    expect(a == b) << field("case", "identical construction must reuse the node");
    expect(bounds(a) == bounds(b))
        << report(field("case", "identical construction must reuse the bound"),
                  field("expected", to_string(bounds(a))),
                  field("actual", to_string(bounds(b))));

    std::mt19937_64 rng(pick_seed());
    for (int iter = 0; iter < 200; iter++) {
      VarSet vs = make_vars(rng, 2, 4, kMaxPoints);

      const std::uint64_t branch = rng();
      std::mt19937_64 r1(branch);
      std::mt19937_64 r2(branch);

      Gen g1{r1, vs.vars, {}, true, true, true, 2, 5};
      Gen g2{r2, vs.vars, {}, true, true, true, 2, 5};

      const GenNode n1 = gen(g1, 0);
      const GenNode n2 = gen(g2, 0);

      expect(n1.dump == n2.dump)
          << report(field("case", "same rng stream must generate the same tree"),
                    field("expected", n1.dump),
                    field("actual", n2.dump),
                    field("iteration", std::to_string(iter)));
      expect(n1.expr == n2.expr)
          << report(field("case", "same tree must intern to the same node"),
                    field("expr", n1.dump),
                    field("iteration", std::to_string(iter)));
      expect(bounds(n1.expr) == bounds(n2.expr))
          << report(field("case", "same node must carry the same bound"),
                    field("expected", to_string(bounds(n1.expr))),
                    field("actual", to_string(bounds(n2.expr))),
                    field("iteration", std::to_string(iter)));
    }
  };
};

}  // namespace
