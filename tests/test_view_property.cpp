// ===========================================================================
// test_view_property.cpp — the centerpiece fuzzer.
//
// Starting from `iota(random small shape)`, apply a random sequence (length
// 4-10) of movement ops to BOTH the dense oracle and the `View`, generating
// only VALID arguments for the CURRENT shape. After the sequence, enumerate
// every logical index of the final shape and assert `resolve(view, idx)`
// equals the oracle cell id (both possibly `nullopt`).
//
// INDEPENDENCE: the oracle reaches its answer purely by dense cell transport.
// There is no shared physical-index helper with `View`.
//
// RED phase: `View`'s ops are stubs, so this fails at runtime. On failure it
// prints the seed, iteration, the replayable op sequence, the failing logical
// index, and expected/actual — the reproducibility harness for the GREEN phase.
// ===========================================================================
import std;
import boost.ut;
import atml.core;
import test_oracle;

namespace ut = boost::ut;

namespace {

constexpr std::int64_t kNumelBound = 2048;  // keep total cells bounded

std::vector<atml::Dim> to_dims(const std::vector<std::int64_t>& xs) {
  std::vector<atml::Dim> out;
  out.reserve(xs.size());
  for (const std::int64_t x : xs)
    out.emplace_back(x);
  return out;
}

std::vector<std::pair<atml::Dim, atml::Dim>>
to_dim_ranges(const std::vector<std::pair<std::int64_t, std::int64_t>>& rs) {
  std::vector<std::pair<atml::Dim, atml::Dim>> out;
  out.reserve(rs.size());
  for (const auto& [lo, hi] : rs)
    out.emplace_back(atml::Dim{lo}, atml::Dim{hi});
  return out;
}

std::string vec_dump(const std::vector<std::int64_t>& xs) {
  std::string s = "[";
  for (std::size_t i = 0; i < xs.size(); i++)
    s += std::to_string(xs[i]) + (i + 1 < xs.size() ? "," : "");
  return s + "]";
}

// The parallel state advanced by each op: the real oracle, the (stubbed) view,
// whether the view is still contiguous (gates reshape), and a replayable dump.
struct State {
  oracle::DenseRef ref;
  atml::View view;
  bool contiguous = true;
  std::vector<std::string> dump;
};

ut::suite view_property_suite = [] {
  using namespace ut;
  using namespace atml;

  "property: random op sequences match the dense oracle"_test = [] {
    constexpr int iterations = 500;

    std::uint64_t seed = 0xa71ec0ffeeull;
    if (const char* env = std::getenv("ATML_TEST_SEED"))
      seed = std::strtoull(env, nullptr, 10);
    std::mt19937_64 rng(seed);

    auto uni = [&rng](std::int64_t lo, std::int64_t hi) {
      return std::uniform_int_distribution<std::int64_t>(lo, hi)(rng);
    };

    // --- the six op generators; each mutates `s` in lockstep --------------

    auto do_permute = [&](State& s) {
      const std::size_t r = s.ref.shape.size();
      std::vector<std::size_t> perm(r);
      std::iota(perm.begin(), perm.end(), std::size_t{0});
      std::shuffle(perm.begin(), perm.end(), rng);
      s.ref = oracle::permute(s.ref, perm);
      s.view = permute(s.view, perm);
      s.contiguous = false;
      s.dump.push_back("permute(" + vec_dump(std::vector<std::int64_t>(
          perm.begin(), perm.end())) + ")");
    };

    auto do_expand = [&](State& s) {
      std::vector<std::int64_t> ns = s.ref.shape;
      std::int64_t n = oracle::numel(ns);
      for (std::size_t a = 0; a < ns.size(); a++)
        if (ns[a] == 1) {
          const std::int64_t f = uni(1, 4);
          if (n * f <= kNumelBound) {
            ns[a] = f;
            n *= f;
          }
        }
      s.ref = oracle::expand(s.ref, ns);
      s.view = expand(s.view, to_dims(ns));
      s.contiguous = false;
      s.dump.push_back("expand(" + vec_dump(ns) + ")");
    };

    auto do_shrink = [&](State& s) {
      std::vector<std::pair<std::int64_t, std::int64_t>> ranges(s.ref.shape.size());
      for (std::size_t a = 0; a < s.ref.shape.size(); a++) {
        const std::int64_t size = s.ref.shape[a];
        const std::int64_t lo = uni(0, size - 1);        // 0 <= lo < size
        const std::int64_t hi = uni(lo + 1, size);       // lo < hi <= size
        ranges[a] = {lo, hi};
      }
      s.ref = oracle::shrink(s.ref, ranges);
      s.view = shrink(s.view, to_dim_ranges(ranges));
      s.contiguous = false;
      std::string d = "shrink([";
      for (const auto& [lo, hi] : ranges)
        d += "(" + std::to_string(lo) + "," + std::to_string(hi) + ")";
      s.dump.push_back(d + "])");
    };

    auto do_flip = [&](State& s) {
      std::vector<std::size_t> axes;
      for (std::size_t a = 0; a < s.ref.shape.size(); a++)
        if (uni(0, 1) == 1)
          axes.push_back(a);
      s.ref = oracle::flip(s.ref, axes);
      s.view = flip(s.view, axes);
      s.contiguous = false;
      s.dump.push_back("flip(" + vec_dump(std::vector<std::int64_t>(
          axes.begin(), axes.end())) + ")");
    };

    auto do_pad = [&](State& s) {
      std::vector<std::pair<std::int64_t, std::int64_t>> pads(s.ref.shape.size());
      std::int64_t running = oracle::numel(s.ref.shape);
      for (std::size_t a = 0; a < s.ref.shape.size(); a++) {
        std::int64_t before = uni(0, 2);
        std::int64_t after = uni(0, 2);
        // Clamp so the running element count stays bounded.
        const std::int64_t old = s.ref.shape[a];
        while (old != 0 &&
               running / old * (old + before + after) > kNumelBound &&
               (before > 0 || after > 0)) {
          if (after > 0) after--; else before--;
        }
        if (old != 0)
          running = running / old * (old + before + after);
        pads[a] = {before, after};
      }
      s.ref = oracle::pad(s.ref, pads);
      s.view = pad(s.view, to_dim_ranges(pads));
      s.contiguous = false;
      std::string d = "pad([";
      for (const auto& [b, a] : pads)
        d += "(" + std::to_string(b) + "," + std::to_string(a) + ")";
      s.dump.push_back(d + "])");
    };

    auto do_reshape = [&](State& s) {
      // Precondition already checked by caller: s.contiguous is true.
      const std::int64_t n = oracle::numel(s.ref.shape);
      std::vector<std::int64_t> ns;
      if (n <= 1) {
        ns = {n};
      } else {
        // Pick a proper divisor to make a rank-2 shape {d, n/d}.
        std::vector<std::int64_t> divisors;
        for (std::int64_t d = 1; d <= n; d++)
          if (n % d == 0)
            divisors.push_back(d);
        const std::int64_t d = divisors[static_cast<std::size_t>(
            uni(0, static_cast<std::int64_t>(divisors.size()) - 1))];
        ns = {d, n / d};
      }
      s.ref = oracle::reshape(s.ref, ns);
      const std::optional<View> rv = reshape(s.view, to_dims(ns));
      expect(rv.has_value()) << "contiguous reshape must succeed";
      if (rv)
        s.view = *rv;
      s.contiguous = true;  // result is freshly contiguous
      s.dump.push_back("reshape(" + vec_dump(ns) + ")");
    };

    // ---------------------------------------------------------------------

    for (int iter = 0; iter < iterations; iter++) {
      // Random small start shape: rank 2-4, each extent 2-4 (numel <= 256).
      const std::size_t rank = static_cast<std::size_t>(uni(2, 4));
      std::vector<std::int64_t> start(rank);
      for (auto& e : start)
        e = uni(2, 4);

      State s{oracle::iota(start), contiguous(to_dims(start)), true, {}};
      const std::string start_dump = vec_dump(start);

      const int ops = static_cast<int>(uni(4, 10));
      for (int k = 0; k < ops; k++) {
        // Assemble the ops valid for the current state.
        std::vector<int> choices = {0, 2, 3, 4};  // permute, shrink, flip, pad
        bool has_one = false;
        for (const std::int64_t e : s.ref.shape)
          has_one = has_one || (e == 1);
        if (has_one)
          choices.push_back(1);       // expand needs a size-1 axis
        if (s.contiguous)
          choices.push_back(5);       // reshape needs a contiguous view

        switch (choices[static_cast<std::size_t>(
            uni(0, static_cast<std::int64_t>(choices.size()) - 1))]) {
          case 0: do_permute(s); break;
          case 1: do_expand(s);  break;
          case 2: do_shrink(s);  break;
          case 3: do_flip(s);    break;
          case 4: do_pad(s);     break;
          default: do_reshape(s); break;
        }
      }

      // Compare every logical index against the oracle.
      std::string seq;
      for (std::size_t i = 0; i < s.dump.size(); i++)
        seq += s.dump[i] + (i + 1 < s.dump.size() ? " -> " : "");

      if (oracle::numel(s.ref.shape) == 0)
        continue;  // (does not occur: every op keeps numel >= 1)

      std::vector<std::int64_t> idx(s.ref.shape.size(), 0);
      do {
        const std::optional<std::int64_t> got = resolve(s.view, idx);
        const oracle::Cell want = s.ref.at(idx);
        if (got != want) {
          expect(false) << "FAIL seed=" << seed << " iter=" << iter
              << " start=" << start_dump << " seq=" << seq
              << " at=" << vec_dump(idx)
              << " want=" << (want ? std::to_string(*want) : "nullopt")
              << " got=" << (got ? std::to_string(*got) : "nullopt");
          break;  // one report per iteration is enough to reproduce
        }
      } while (oracle::next_index(idx, s.ref.shape));
    }
  };
};

}  // namespace
