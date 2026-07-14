export module atml.core:view;

import std;
import :dim;
import :sym;

constexpr std::vector<atml::Dim> get_strides(const std::vector<atml::Dim>& shape) {
  std::int64_t dims = shape.size();
  std::vector<atml::Dim> strides(dims, atml::Dim{1});
  atml::Dim acc{std::int64_t{1}};

  for (auto x = dims - 1; x >= 0; x--) {
    strides[x] = acc;
    acc *= shape[x];
  }

  return strides;
}

export namespace atml {
  // Per-dim valid half-open window [lo, hi) carried alongside a padded view.
  using Mask = std::vector<std::pair<Dim, Dim>>;

  struct View {
    std::vector<Dim> shape;
    std::vector<Dim> strides;
    Dim offset;                       // Physical flat index of logical index 0
    std::optional<Mask> mask;         // Records the valid window introduced by padding
  };

  // Returns a row-major contiguous view
  View contiguous(const std::vector<Dim>& shape) {
    return View{
      .shape = shape,
      .strides = get_strides(shape),
      .offset = Dim{std::int64_t{0}},
      .mask = std::nullopt,
    };
  }

  // Reorder axes by `perm` (metadata-only transpose of shape, strides, mask).
  View permute(const View& view, const std::vector<std::size_t>& perm) {
    std::vector<Dim> shape(perm.size());
    std::vector<Dim> strides(perm.size());

    for (std::size_t i = 0; i < perm.size(); i++) {
      shape[i] = view.shape[perm[i]];
      strides[i] = view.strides[perm[i]];
    }

    std::optional<Mask> mask = std::nullopt;
    if (view.mask) {
      Mask permuted(perm.size());
      for (std::size_t i = 0; i < perm.size(); i++)
        permuted[i] = (*view.mask)[perm[i]];
      mask = std::move(permuted);
    }

    return View{
      .shape = std::move(shape),
      .strides = std::move(strides),
      .offset = view.offset,
      .mask = std::move(mask),
    };
  }

  // Broadcast size-1 axes up to `new_shape` (stride 0 on the grown axes).
  constexpr View expand(const View& view, const std::vector<Dim>& new_shape) {
    auto res = view;

    for (std::size_t x = 0; x < new_shape.size(); x++) {
      auto opt = dim_lt(view.shape[x], new_shape[x]);

      if (opt.has_value() and opt.value())
        res.strides[x] = 0;
    }

    res.shape = new_shape;

    return res;
  }

  // Slice each axis to the half-open range [lo, hi); shifts the offset.
  View shrink(const View& view,
              const std::vector<std::pair<Dim, Dim>>& ranges) {
    auto res = view;

    std::size_t idx = -1;
    for (auto&& [val, range] : std::views::zip(res.shape, ranges)) {
      auto lo = std::get_if<std::int64_t>(&range.first);
      auto hi = std::get_if<std::int64_t>(&range.second);

      idx++;
      if (not (lo and hi))
        continue;

      val = Dim{*hi - *lo};

      if (view.mask.has_value()) {
        auto mask = view.mask.value()[idx];

        auto mask_lo = std::get_if<std::int64_t>(&mask.first);
        auto mask_hi = std::get_if<std::int64_t>(&mask.second);

        if (mask_lo and mask_hi)
          mask = std::pair{
            std::max(std::int64_t{0}, *mask_lo - *lo), std::min(*hi - *lo, *mask_hi - *lo)};
      }
    }

    res.offset = std::inner_product(
      ranges.begin(), ranges.end(),
      view.strides.begin(),
      res.offset,
      std::plus<>{},
      [](const auto& range, const auto& stride) {
        return range.first * stride;
      });

    return res;
  }

  // Reverse cell order along each listed axis (negates stride, absorbs the
  // reversal into the offset so no physical index ever goes negative).
  View flip(const View& view, const std::vector<std::size_t>& axes) {
    View res = view;

    for (const auto& axis : axes) {
      res.offset += (view.shape[axis] - 1) * view.strides[axis];
      res.strides[axis] *= -1;

      if (res.mask.has_value()) {
        auto mask = res.mask.value()[axis];
        res.mask.value()[axis] = std::pair{
          res.shape[axis] - mask.second, 
          res.shape[axis] - mask.first};
      }
    }

    return res;
  }

  // Enlarge each axis by (before, after); the new border cells are invalid,
  // recorded in the mask.
  View pad(const View& view, const std::vector<std::pair<Dim, Dim>>& pads) {
    auto res = view;
    std::size_t dims = view.shape.size();
    Dim offset_shift{std::int64_t{0}};

    if (not res.mask.has_value()) {
      std::vector<std::pair<Dim, Dim>> mask(dims);

      std::size_t idx = 0;
      auto&& shape = res.shape;
      std::generate(mask.begin(), mask.end(), [&]() {
          return std::pair{0, shape[idx++]};
      });

      res.mask = mask;
    }

    for (std::size_t x = 0; x < dims; x++) {
      auto [before, after] = pads[x];
      auto mask = res.mask.value()[x];

      res.shape[x] += before + after;
      offset_shift += before * res.strides[x];
      mask = {mask.first + before, mask.second + before};
      res.mask.value()[x] = mask;
    }

    res.offset -= offset_shift;

    return res;
  }

  // DECISION: On Day 2 reshape succeeds iff the input view is contiguous, in
  // which case it re-lays-out row-major into `new_shape`; on any non-contiguous
  // view it returns `nullopt`. The adjacent-dim merge / multi-view story is
  // deferred to Day 3 so the whole merge lives in one place.
  std::optional<View> reshape(const View& view,
                              const std::vector<Dim>& new_shape) {

    auto offset = std::get_if<std::int64_t>(&view.offset);
    if (not (offset or *offset == 0 or not view.mask.has_value()))
      return std::nullopt;

    std::int64_t acc = 1, dims = view.shape.size();
    for (std::int64_t x = dims - 1; x >= 0; x--) {
      auto dim = std::get_if<std::int64_t>(&view.shape[x]);
      auto stride = std::get_if<std::int64_t>(&view.strides[x]);

      if (not (dim and stride))
        std::nullopt;

      if (*dim == 1)
        continue;

      if (*stride != acc)
        return std::nullopt;

      acc *= *dim;
    }

    return contiguous(new_shape);
  }

  // True iff no Sym appears anywhere in shape/strides/offset/mask, so that
  // `resolve` may be called.
  constexpr bool is_concrete(const View& v) {
    bool is_concrete_shape = std::all_of(v.shape.cbegin(), v.shape.cend(),
      [](auto&& x){
        return std::holds_alternative<std::int64_t>(x);
      });

    bool is_concrete_strides = std::all_of(v.strides.cbegin(),
        v.strides.cend(), [](auto&& x){
        return std::holds_alternative<std::int64_t>(x);
      });

    bool is_concrete_mask = true;
    if (v.mask.has_value()) {
      auto mask = v.mask.value();

      is_concrete_mask = std::all_of(mask.cbegin(), mask.cend(), [](auto&& x){
          return std::holds_alternative<std::int64_t>(x.first) and
          std::holds_alternative<std::int64_t>(x.second);
      });
    }

    bool is_concrete_offset = std::holds_alternative<std::int64_t>(v.offset);

    return is_concrete_offset and is_concrete_shape and is_concrete_strides and is_concrete_mask;
  }

  // Physical flat index for a fully-concrete logical index, or `nullopt` when
  // the index is masked out. Precondition: the View is concrete (no Sym).
  // Because the origin buffer is contiguous, this physical flat index equals
  // the origin id the oracle transports.
  constexpr std::optional<std::int64_t> resolve(const View& view,
                                      const std::vector<std::int64_t>& indeces) {
    if (not is_concrete(view))
      return std::nullopt;

    Dim dot_product = std::inner_product(indeces.begin(),
      indeces.end(), view.strides.begin(), std::int64_t{0}, std::plus<>{},
      [](std::int64_t x, const Dim& dim) {
          return x * std::get<std::int64_t>(dim);
      });

    if (view.mask.has_value()) {
      for (std::size_t axis = 0; axis < indeces.size(); axis++) {
        const auto lo = std::get_if<std::int64_t>(&view.mask.value()[axis].first);
        const auto hi = std::get_if<std::int64_t>(&view.mask.value()[axis].second);

        if (indeces[axis] < *lo or indeces[axis] >= *hi)
          return std::nullopt;
      }
    }

    return std::get<std::int64_t>(view.offset + dot_product);
  }
}
