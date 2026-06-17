#include <boost/ut.hpp>

#include "shape_tracker.hpp"

namespace ut = boost::ut;

static ut::suite shape_tracker_suite = [] {
  using namespace ut;
  using namespace atml;

  "row_major_strides and numel"_test = [] {
     expect(row_major_strides({2, 3, 4}) == Strides{12, 4, 1});

     View view = {
       .shape = {2, 3, 4},
       .strides = row_major_strides({2, 3, 4}),
       .offset = 0,
       .mask = std::nullopt,
     };

     expect(view.numel() == 24);
     expect(view.contiguous());
  };

  "flat offset, indexing and broadcast"_test = [] {
    View view = {
      .shape = {2, 3},
      .strides = row_major_strides({2, 3}),
      .offset = 0,
      .mask = std::nullopt,
    };

    expect(flat_offset(view, std::array<dim_t, 2>{1, 2}) == 5);

    view.offset = 10;
    expect(flat_offset(view, std::array<dim_t, 2>{0, 0}) == 10);

    View bc = {
      .shape = {4, 3},
      .strides = {0, 1},
      .offset = 0,
      .mask = std::nullopt,
    };

    expect(flat_offset(bc, std::array<dim_t, 2>{0, 2}) ==
        flat_offset(bc, std::array<dim_t, 2>{3, 2}));
  };

  "permutation reordering"_test = [] {
    View view = {
      .shape = {2, 3},
      .strides = row_major_strides({2, 3}),
      .offset = 0,
      .mask = std::nullopt,
    };

    View perm = permute(view, std::array<dim_t, 2>{1, 0});

    expect(perm.shape == Shape{3, 2});
    expect(perm.strides == Strides{1, 3});
    expect(not perm.contiguous());

    expect(flat_offset(perm, std::array<dim_t, 2>{2, 1}) 
        == flat_offset(view, std::array<dim_t, 2>{1, 2}));
  };

  "expand sets stride 0 in 1D tensor"_test = [] {
    View view = {
      .shape = {1, 3},
      .strides = row_major_strides({1, 3}),
      .offset = 0,
      .mask = std::nullopt,
    };

    View expanded = expand(view, Shape{4, 3});

    expect(expanded.shape == Shape{4, 3});
    expect(expanded.strides[0] == 0);
    expect(expanded.strides[1] == 1);
  };

  "reshape sanity"_test = [] {
    // Collapse on contiguous, fail in non-continguous
    View view = {
      .shape = {2, 3},
      .strides = row_major_strides({2, 3}),
      .offset = 0,
      .mask = std::nullopt,
    };

    auto res = reshape(view, {6});
    (void)(expect(res.has_value()) >> fatal);

    expect(res->strides == Strides{1});

    View perm = permute(view, std::array<dim_t, 2>{1, 0});
    expect(not reshape(perm, {6}).has_value());
  };

  "shape tracker basics"_test = [] {
    ShapeTracker tracker = {
      .views = {
        View {
          .shape = {2, 3},
          .strides = row_major_strides({2, 3}),
          .offset = 0,
          .mask = std::nullopt,
        },
      }
    };

    expect(tracker.shape() == Shape{2, 3});
    expect(tracker.numel() == 6);
    expect(tracker.contiguous());

    tracker.views.push_back(tracker.top());
    expect(not tracker.contiguous());
  };
};
