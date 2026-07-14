// ===========================================================================
// test_view_symbolic.cpp — light symbolic flow (NO materialization).
//
// Build views whose shapes carry a symbolic `Dim`, push them through the
// metadata edits, and assert the resulting shape/strides/offset are the
// EXPECTED `Dim` expressions — checked structurally via `dim_eq` (accepting
// nullopt-as-unknown per Day-1 rules) and numerically via `dim_eval` under a
// binding {pos: 5}. `resolve` is intentionally never called: the View is
// symbolic, and materialization is out of scope this week.
//
// RED phase: `contiguous`/`permute`/... are stubs returning empty views, so the
// shape/stride assertions fail at runtime.
// ===========================================================================
import std;
import boost.ut;
import atml.core;

namespace ut = boost::ut;

namespace {

// A definite `dim_eq`: the two dims must be provably equal (not unknown).
bool dims_equal(const atml::Dim& a, const atml::Dim& b) {
  const std::optional<bool> eq = atml::dim_eq(a, b);
  return eq.has_value() && *eq;
}

ut::suite view_symbolic_suite = [] {
  using namespace ut;
  using namespace atml;

  "permute flows a symbolic dim through shape and strides"_test = [] {
    const Bindings bind{{"pos", 5}};
    const Dim pos{sym("pos")};

    // contiguous [4, pos]: row-major strides [pos, 1], offset 0. The leading
    // stride is the trailing extent — symbolic.
    const View v = contiguous({Dim{std::int64_t{4}}, pos});
    expect(v.shape.size() == 2u) << "rank preserved";
    if (v.shape.size() == 2u && v.strides.size() == 2u) {
      expect(dims_equal(v.shape[0], Dim{std::int64_t{4}}));
      expect(dims_equal(v.shape[1], pos));
      expect(dims_equal(v.strides[0], pos));               // symbolic stride
      expect(dims_equal(v.strides[1], Dim{std::int64_t{1}}));
      expect(dim_eval(v.strides[0], bind) == 5) << "stride0 == pos == 5";
    }

    // permute to [pos, 4]: strides become [1, pos].
    const View p = permute(v, {1, 0});
    expect(p.shape.size() == 2u) << "permute preserves rank";
    if (p.shape.size() == 2u && p.strides.size() == 2u) {
      expect(dims_equal(p.shape[0], pos));
      expect(dims_equal(p.shape[1], Dim{std::int64_t{4}}));
      expect(dims_equal(p.strides[0], Dim{std::int64_t{1}}));
      expect(dims_equal(p.strides[1], pos));
      expect(dim_eval(p.shape[0], bind) == 5) << "shape0 == pos == 5";
      expect(dim_eval(p.strides[1], bind) == 5) << "stride1 == pos == 5";
    }
  };

  "expand grows a size-1 axis to stride 0 alongside a symbolic dim"_test = [] {
    const Bindings bind{{"pos", 5}};
    const Dim pos{sym("pos")};

    // contiguous [1, pos]: strides [pos, 1]. Broadcast axis 0 (size 1) to 5.
    const View v = contiguous({Dim{std::int64_t{1}}, pos});
    const View e = expand(v, {Dim{std::int64_t{5}}, pos});
    expect(e.shape.size() == 2u) << "expand preserves rank";
    if (e.shape.size() == 2u && e.strides.size() == 2u) {
      expect(dims_equal(e.shape[0], Dim{std::int64_t{5}}));
      expect(dims_equal(e.shape[1], pos));
      expect(dims_equal(e.strides[0], Dim{std::int64_t{0}})) << "broadcast axis";
      expect(dims_equal(e.strides[1], Dim{std::int64_t{1}}));
      expect(dim_eval(e.shape[1], bind) == 5) << "shape1 == pos == 5";
    }
  };

  "shrink with concrete ranges keeps the symbolic extent"_test = [] {
    const Bindings bind{{"pos", 5}};
    const Dim pos{sym("pos")};

    // contiguous [pos, 4]: strides [4, 1]. Slice axis 0 full [0,pos), axis 1
    // concrete [1,3): shape [pos, 2], offset 0*4 + 1*1 = 1.
    const View v = contiguous({pos, Dim{std::int64_t{4}}});
    const View sr = shrink(v, {{Dim{std::int64_t{0}}, pos},
                               {Dim{std::int64_t{1}}, Dim{std::int64_t{3}}}});
    expect(sr.shape.size() == 2u) << "shrink preserves rank";
    if (sr.shape.size() == 2u) {
      expect(dims_equal(sr.shape[0], pos)) << "symbolic extent survives";
      expect(dims_equal(sr.shape[1], Dim{std::int64_t{2}}));
      expect(dims_equal(sr.offset, Dim{std::int64_t{1}})) << "offset absorbs lo";
      expect(dim_eval(sr.shape[0], bind) == 5) << "shape0 == pos == 5";
    }
  };
};

}  // namespace
