import std;
import boost.ut;
import atml.core;

namespace ut = boost::ut;

// Test-local oracle for the pinned floor-division convention (quotient
// rounds toward -inf, mod sign follows the divisor) — duplicated from
// test_sym.cpp on purpose: each test file is self-contained.
static std::int64_t fdiv_oracle(std::int64_t a, std::int64_t b) {
  std::int64_t q = a / b;

  if (a % b != 0 and ((a < 0) != (b < 0)))
    q--;

  return q;
}

static std::int64_t mod_oracle(std::int64_t a, std::int64_t b) {
  return a - fdiv_oracle(a, b) * b;
}

static ut::suite dim_suite = [] {
  using namespace ut;
  using namespace atml;

  "int (op) int stays in the int64_t alternative"_test = [] {
    const Dim a{std::int64_t{6}};
    const Dim b{std::int64_t{7}};

    const Dim sum = a + b;
    const auto* s = std::get_if<std::int64_t>(&sum);
    expect(s != nullptr and *s == 13);

    const Dim prod = a * b;
    const auto* p = std::get_if<std::int64_t>(&prod);
    expect(p != nullptr and *p == 42);

    const Dim quot = floordiv(b, a);
    const auto* q = std::get_if<std::int64_t>(&quot);
    expect(q != nullptr and *q == 1);

    const Dim rem = mod(b, a);
    const auto* r = std::get_if<std::int64_t>(&rem);
    expect(r != nullptr and *r == 1);
  };

  "int (op) Sym promotes to a Sym"_test = [] {
    const Bindings binds{{"n", 40}};
    const Dim n = sym("n");
    const Dim two{std::int64_t{2}};

    const Dim sum = two + n;
    expect(std::holds_alternative<Sym>(sum));
    expect(dim_eval(sum, binds) == 42);

    const Dim prod = n * two;
    expect(std::holds_alternative<Sym>(prod));
    expect(dim_eval(prod, binds) == 80);

    const Dim quot = floordiv(n, two);
    expect(std::holds_alternative<Sym>(quot));
    expect(dim_eval(quot, binds) == 20);

    const Dim rem = mod(n, two);
    expect(std::holds_alternative<Sym>(rem));
    expect(dim_eval(rem, binds) == 0);
  };

  "dim_eq/dim_lt: decidable pairs decide, unknowns stay unknown"_test = [] {
    const Dim i3{std::int64_t{3}};
    const Dim i4{std::int64_t{4}};
    const Dim x = sym("x");
    const Dim y = sym("y");

    // int vs int: always has_value, with the right answer.
    expect(dim_eq(i3, i3) == std::optional{true});
    expect(dim_eq(i3, i4) == std::optional{false});
    expect(dim_lt(i3, i4) == std::optional{true});
    expect(dim_lt(i4, i3) == std::optional{false});
    expect(dim_lt(i3, i3) == std::optional{false});

    // Identical interned Sym vs itself: definitely equal.
    expect(dim_eq(x, x) == std::optional{true});

    // Genuinely unknown comparisons: nullopt is acceptable — assert only
    // that no WRONG definite answer comes back (either definite answer is
    // wrong for some binding).
    expect(dim_eq(x, y) != std::optional{true});
    expect(dim_eq(x, y) != std::optional{false});
    expect(dim_lt(x, i3) != std::optional{true});
    expect(dim_lt(x, i3) != std::optional{false});
    expect(dim_lt(x, x) != std::optional{true});
  };

  "property: dim_eval distributes over dim arithmetic"_test = [] {
    constexpr int iterations = 100;

    std::uint64_t seed = 0xd1a1b2c3ull;
    if (const char* env = std::getenv("ATML_TEST_SEED"))
      seed = std::strtoull(env, nullptr, 10);

    std::mt19937_64 rng(seed);
    const std::array<std::string_view, 4> pool{"a", "b", "c", "d"};
    std::uniform_int_distribution<std::int64_t> leaf(-64, 64);

    for (int iter = 0; iter < iterations; iter++) {
      Bindings binds;
      for (const auto name : pool)
        binds.emplace(std::string{name}, leaf(rng));

      auto rand_dim = [&]() -> Dim {
        if (std::uniform_int_distribution<int>(0, 1)(rng) == 0)
          return leaf(rng);

        std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
        return sym(pool[pick(rng)]);
      };

      const Dim a = rand_dim();
      const Dim b = rand_dim();
      const std::int64_t ea = dim_eval(a, binds);
      const std::int64_t eb = dim_eval(b, binds);

      expect(dim_eval(a + b, binds) == ea + eb)
          << "seed=" << seed << "iter=" << iter << "op=+";
      expect(dim_eval(a * b, binds) == ea * eb)
          << "seed=" << seed << "iter=" << iter << "op=*";

      if (eb != 0) {  // div/mod-by-zero guarded at the call site
        expect(dim_eval(floordiv(a, b), binds) == fdiv_oracle(ea, eb))
            << "seed=" << seed << "iter=" << iter << "op=fdiv";
        expect(dim_eval(mod(a, b), binds) == mod_oracle(ea, eb))
            << "seed=" << seed << "iter=" << iter << "op=mod";
      }
    }
  };
};
