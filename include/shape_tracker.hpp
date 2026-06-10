#pragma once

#include <vector>
#include <ranges>
#include <algorithm>
#include <optional>
#include <utility>
#include <span>

namespace atml {
  using dim_t = std::size_t;
  using sdim_t = std::ptrdiff_t;
  using Shape = std::vector<dim_t>;
  using Strides = std::vector<sdim_t>;

  inline Strides row_major_strides(const Shape& shape) {
    Strides strides(shape.size());

    sdim_t acc = 1;
    auto idx = shape.size();

    while (idx-- > 0) {
      strides[idx] = acc;
      acc *= sdim_t{shape[idx]};
    }

    return strides;
  }

  struct View {
    Shape     shape;
    Strides   strides;
    sdim_t    offset;
    std::optional<std::vector<std::pair<dim_t, dim_t>>> mask;

    dim_t numel() const {
      return std::ranges::fold_left(shape, dim_t{1}, std::multiplies{});
    }

    bool contiguous() const {
      return (not mask.has_value()) and offset == 0 
        and strides == row_major_strides(shape);
    }
  };

  struct ShapeTracker {
    std::vector<View> views;

    decltype(auto) top(this auto&& self) {
      return self.views.back();
    }

    decltype(auto) shape(this auto&& self) {
      return self.top().shape;
    }

    dim_t numel() const {
      return top().numel();
    }

    bool contiguous() const {
      return views.size() == 1 and top().contiguous();
    }
  };

  View permute(const View&, std::span<const dim_t>);
  View expand(const View&, const Shape&);
  std::optional<View> reshape(const View&, const Shape&); 
  sdim_t flat_offset(const View&, std::span<const dim_t>);
}
