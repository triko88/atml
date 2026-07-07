import std;
import boost.ut;
import atml.core;

namespace ut = boost::ut;

// Test-local oracle for the pinned floor-division convention: the quotient
// rounds toward -inf and the mod result's sign follows the divisor, so that
// a == floordiv(a,b)*b + mod(a,b) always holds.
static std::int64_t fdiv_oracle(std::int64_t a, std::int64_t b) {
  std::int64_t q = a / b;

  if (a % b != 0 and ((a < 0) != (b < 0)))
    q--;

  return q;
}

static std::int64_t mod_oracle(std::int64_t a, std::int64_t b) {
  return a - fdiv_oracle(a, b) * b;
}

static ut::suite sym_suite = [] {
  using namespace ut;
  using namespace atml;

  "interning and structural equality"_test = [] {
    expect(sizeof(Sym) == sizeof(void*));  // cheap, pointer-sized handle

    expect(sym("x") == sym("x"));          // the same interned node
    expect(sym("x") != sym("y"));

    const Sym a = (sym("x") + 1) * 2;
    const Sym b = (sym("x") + 1) * 2;
    expect(a == b);                        // structurally identical => same node
  };

  "constant folding"_test = [] {
    expect(as_const(sym_const(3) + sym_const(4)) == 7);
    expect(as_const(sym_const(6) * sym_const(7)) == 42);
    expect(as_const(sym_const(5)) == 5);
    expect(not as_const(sym("x")).has_value());

    // x + 0 is NOT required to fold; only its value under eval is pinned.
    const Bindings binds{{"x", 5}};
    expect(eval(sym("x") + 0, binds) == 5);
  };

  "algebraic semantics via eval"_test = [] {
    const Bindings binds{{"x", 7}, {"y", -3}};

    expect(eval(sym("x") + sym("y"), binds) == 4);
    expect(eval((sym("x") + 1) * 2, binds) == 16);
    expect(eval(2 * sym("y") + 1, binds) == -5);
    expect(eval(sym("x") * sym("y"), binds) == -21);
  };

  "floordiv/mod pin the floor-division convention"_test = [] {
    const Bindings none{};

    // DECISION: floor division — quotient rounds toward -inf, mod sign
    // follows the divisor, a == floordiv(a,b)*b + mod(a,b).
    expect(eval(floordiv(sym_const(-7), sym_const(2)), none) == -4);
    expect(eval(mod(sym_const(-7), sym_const(2)), none) == 1);
    expect(eval(floordiv(sym_const(7), sym_const(-2)), none) == -4);
    expect(eval(mod(sym_const(7), sym_const(-2)), none) == -1);
    expect(eval(floordiv(sym_const(-7), sym_const(-2)), none) == 3);
    expect(eval(mod(sym_const(-7), sym_const(-2)), none) == -1);

    for (const std::int64_t a : {-9, -7, -1, 0, 4, 11})
      for (const std::int64_t b : {-5, -2, 3, 7}) {
        const std::int64_t q = eval(floordiv(sym_const(a), sym_const(b)), none);
        const std::int64_t r = eval(mod(sym_const(a), sym_const(b)), none);

        expect(q * b + r == a) << "a=" << a << "b=" << b;
        expect(r == mod_oracle(a, b)) << "a=" << a << "b=" << b;
      }
  };

  "property: eval matches a parallel direct-arithmetic fold"_test = [] {
    constexpr int iterations = 500;
    constexpr int max_depth = 6;
    constexpr std::int64_t mul_bound = std::int64_t{1} << 40;

    std::uint64_t seed = 0xa73a11c0deull;
    if (const char* env = std::getenv("ATML_TEST_SEED"))
      seed = std::strtoull(env, nullptr, 10);

    std::mt19937_64 rng(seed);
    const std::array<std::string_view, 4> pool{"a", "b", "c", "d"};
    std::uniform_int_distribution<std::int64_t> leaf(-64, 64);

    for (int iter = 0; iter < iterations; iter++) {
      Bindings binds;
      for (const auto name : pool)
        binds.emplace(std::string{name}, leaf(rng));

      // One generated node: the Sym plus its direct-arithmetic mirror and a
      // readable dump, all folded in parallel as the tree is built — the
      // mirror is never computed by re-walking the Sym.
      struct Node {
        Sym expr;
        std::int64_t mirror;
        std::string dump;
      };

      auto gen = [&](auto&& self, int depth) -> Node {
        // At max depth force a leaf (choices 0-1).
        std::uniform_int_distribution<int> op(0, depth >= max_depth ? 1 : 5);
        const int choice = op(rng);

        switch (choice) {
          case 0: {  // constant leaf
            const std::int64_t v = leaf(rng);
            return {sym_const(v), v, std::to_string(v)};
          }
          case 1: {  // variable leaf
            std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
            const auto name = pool[pick(rng)];
            return {sym(name), binds.find(name)->second, std::string{name}};
          }
          case 2:
          case 3: {  // + or *
            const Node l = self(self, depth + 1);
            const Node r = self(self, depth + 1);

            // DECISION: degrade * to + whenever the mirror product would
            // leave ±2^40, keeping the int64 oracle free of overflow UB.
            const bool mul = choice == 3 and (l.mirror == 0
                or std::abs(r.mirror) <= mul_bound / std::abs(l.mirror));

            if (mul)
              return {l.expr * r.expr, l.mirror * r.mirror,
                      "(" + l.dump + " * " + r.dump + ")"};

            return {l.expr + r.expr, l.mirror + r.mirror,
                    "(" + l.dump + " + " + r.dump + ")"};
          }
          default: {  // floordiv or mod, divisor nonzero by construction
            const Node l = self(self, depth + 1);
            std::int64_t d = leaf(rng);
            if (d == 0)
              d = 1;

            if (choice == 4)
              return {floordiv(l.expr, sym_const(d)), fdiv_oracle(l.mirror, d),
                      "fdiv(" + l.dump + ", " + std::to_string(d) + ")"};

            return {mod(l.expr, sym_const(d)), mod_oracle(l.mirror, d),
                    "mod(" + l.dump + ", " + std::to_string(d) + ")"};
          }
        }
      };

      const Node n = gen(gen, 0);
      const std::int64_t got = eval(n.expr, binds);

      expect(got == n.mirror) << "seed=" << seed << "iter=" << iter
          << "expr=" << n.dump << "expected=" << n.mirror << "got=" << got;
    }
  };
};
