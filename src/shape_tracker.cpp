#include "shape_tracker.hpp"

namespace atml {
  View permute(const View& view, std::span<const dim_t> order) {
    View res = view;
    res.shape.clear();
    res.strides.clear();

    for (auto x : order) {
      res.shape.push_back(view.shape[x]);
      res.strides.push_back(view.strides[x]);
    }

    return res;
  }

  View expand(const View& view, const Shape& to) {
    View res = view;
    std::size_t idx = 0;

    while (idx < to.size() and res.shape[idx] == to[idx])
      idx++;

    res.shape[idx] = to[idx];
    res.strides[idx] = 0;

    return res;
  }

  std::optional<View> reshape(const View& view, const Shape& shape) {
    if (not view.contiguous()) [[unlikely]]
      return std::nullopt;

    return View {
      .shape = shape,
        .strides = row_major_strides(shape),
        .offset = 0,
        .mask = std::nullopt,
    };
  }

  sdim_t flat_offset(const View& view, std::span<const dim_t> ids) {
    sdim_t off = view.offset;

    for (std::size_t dim = 0; dim < ids.size(); dim++)
      off += sdim_t(ids[dim]) * view.strides[dim];

    return off;
  }
}
