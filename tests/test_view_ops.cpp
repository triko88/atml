// ===========================================================================
// test_view_ops.cpp — readable per-op regressions for the Day 2 View.
//
// Each case builds a dense reference with `iota`, applies ONE movement op both
// to the oracle and to the `View`, then asserts that resolving every logical
// index of the result matches the oracle cell (origin id, or `nullopt`).
//
// RED phase: the `View` ops are stubs, so `resolve` returns `nullopt` while the
// oracle carries real ids — these tests fail at runtime, as intended.
// ===========================================================================
import std;
import boost.ut;
import atml.core;
import test_oracle;

namespace ut = boost::ut;

namespace {

// Lift concrete extents / ranges into the `Dim`-typed View API.
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

// Walk every logical index of `ref`'s shape and assert View::resolve agrees
// with the oracle cell there. The enumeration is driven by the REAL oracle
// shape (the stub View has no usable shape yet).
void check_all(const atml::View& v, const oracle::DenseRef& ref,
               std::string_view label) {
  using namespace ut;

  if (oracle::numel(ref.shape) == 0) {
    // Zero-size dim: no logical index exists, both paths agree trivially.
    expect(true) << label << ": empty (no cells)";
    return;
  }

  std::vector<std::int64_t> idx(ref.shape.size(), 0);
  do {
    const std::optional<std::int64_t> got = atml::resolve(v, idx);
    const oracle::Cell want = ref.at(idx);

    std::string where;
    for (const std::int64_t i : idx)
      where += std::to_string(i) + ",";

    expect(got == want) << label << " idx=[" << where << "]"
        << " want=" << (want ? std::to_string(*want) : "nullopt")
        << " got=" << (got ? std::to_string(*got) : "nullopt");

    // No physical index ever escapes into the negatives (flip must absorb the
    // reversal into the offset).
    expect(not got.has_value() or *got >= 0) << label << ": negative physical";
  } while (oracle::next_index(idx, ref.shape));
}

ut::suite view_ops_suite = [] {
  using namespace ut;
  using namespace atml;

  "permute: 2-D transpose of [2,3]"_test = [] {
    const auto ref = oracle::permute(oracle::iota({2, 3}), {1, 0});
    const View v = permute(contiguous(to_dims({2, 3})), {1, 0});
    check_all(v, ref, "permute[2,3]->[3,2]");
  };

  "expand: [3,1] -> [3,4] replicates ids"_test = [] {
    const auto ref = oracle::expand(oracle::iota({3, 1}), {3, 4});
    const View v = expand(contiguous(to_dims({3, 1})), to_dims({3, 4}));
    check_all(v, ref, "expand[3,1]->[3,4]");

    // Distinct logical columns of a row resolve to the SAME physical id.
    const auto a = resolve(v, {1, 0});
    const auto b = resolve(v, {1, 3});
    expect(a == b) << "expand should broadcast a single origin id across a row";
  };

  "shrink: middle slice of [6] -> [2,5)"_test = [] {
    const auto ref = oracle::shrink(oracle::iota({6}), {{2, 5}});
    const View v = shrink(contiguous(to_dims({6})), to_dim_ranges({{2, 5}}));
    check_all(v, ref, "shrink[6]->[2,5)");
  };

  "flip: [4] reversed stays in [0,n)"_test = [] {
    const auto ref = oracle::flip(oracle::iota({4}), {0});
    const View v = flip(contiguous(to_dims({4})), {0});
    check_all(v, ref, "flip[4]");
  };

  "pad: [3] -> pad(1,2) = [6]"_test = [] {
    const auto ref = oracle::pad(oracle::iota({3}), {{1, 2}});
    const View v = pad(contiguous(to_dims({3})), to_dim_ranges({{1, 2}}));
    check_all(v, ref, "pad[3]->(1,2)");

    // The single leading and two trailing cells are pad => nullopt.
    expect(resolve(v, {0}) == std::nullopt) << "leading pad cell";
    expect(resolve(v, {4}) == std::nullopt) << "trailing pad cell";
    expect(resolve(v, {5}) == std::nullopt) << "trailing pad cell";
  };

  // --- composition landmines: this is where mask bugs live -----------------

  "compose: pad then flip"_test = [] {
    auto ref = oracle::pad(oracle::iota({3}), {{1, 2}});
    ref = oracle::flip(ref, {0});
    View v = pad(contiguous(to_dims({3})), to_dim_ranges({{1, 2}}));
    v = flip(v, {0});
    check_all(v, ref, "pad->flip");
  };

  "compose: pad then shrink into the padded region"_test = [] {
    // pad [3]->(1,2)=[6], then slice [0,4) which straddles a leading pad cell.
    auto ref = oracle::pad(oracle::iota({3}), {{1, 2}});
    ref = oracle::shrink(ref, {{0, 4}});
    View v = pad(contiguous(to_dims({3})), to_dim_ranges({{1, 2}}));
    v = shrink(v, to_dim_ranges({{0, 4}}));
    check_all(v, ref, "pad->shrink");
  };

  "compose: flip then shrink"_test = [] {
    auto ref = oracle::flip(oracle::iota({6}), {0});
    ref = oracle::shrink(ref, {{1, 4}});
    View v = flip(contiguous(to_dims({6})), {0});
    v = shrink(v, to_dim_ranges({{1, 4}}));
    check_all(v, ref, "flip->shrink");
  };

  "compose: expand then permute"_test = [] {
    auto ref = oracle::expand(oracle::iota({1, 3}), {4, 3});
    ref = oracle::permute(ref, {1, 0});
    View v = expand(contiguous(to_dims({1, 3})), to_dims({4, 3}));
    v = permute(v, {1, 0});
    check_all(v, ref, "expand->permute");
  };

  "reshape: contiguous [6] -> [2,3]; non-contiguous -> nullopt"_test = [] {
    const auto ref = oracle::reshape(oracle::iota({6}), {2, 3});
    const auto rv = reshape(contiguous(to_dims({6})), to_dims({2, 3}));
    expect(rv.has_value()) << "contiguous reshape must succeed";
    if (rv)
      check_all(*rv, ref, "reshape[6]->[2,3]");

    // DECISION (Day 2): reshape of a non-contiguous view is nullopt; the
    // adjacent-dim merge is deferred to Day 3.
    const View permuted = permute(contiguous(to_dims({2, 3})), {1, 0});
    expect(reshape(permuted, to_dims({6})) == std::nullopt)
        << "non-contiguous reshape must decline on Day 2";
  };

  "edge: rank-0 scalar and a size-1 dim"_test = [] {
    // Rank-0 scalar: shape {}, exactly one cell whose id is 0.
    const auto scalar = oracle::iota({});
    const View sv = contiguous(to_dims({}));
    expect(resolve(sv, {}) == std::optional<std::int64_t>{0}) << "scalar id";
    check_all(sv, scalar, "scalar{}");

    // A size-1 dim resolves its single index to id 0.
    const auto one = oracle::iota({1});
    const View ov = contiguous(to_dims({1}));
    check_all(ov, one, "size-1");
  };

  "edge: a zero-size dim resolves trivially"_test = [] {
    const auto empty = oracle::iota({0, 3});
    const View v = contiguous(to_dims({0, 3}));
    // No logical index exists; resolve is never called. Assert the counts
    // agree so this case is not silently skipped elsewhere.
    expect(oracle::numel(empty.shape) == 0) << "zero-size dim => no cells";
    check_all(v, empty, "zero-size");
  };
};

}  // namespace
