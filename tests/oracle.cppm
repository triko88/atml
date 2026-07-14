// ===========================================================================
// oracle.cppm — module `test_oracle`: the REAL dense reference the Day 2 View
// tests are checked against. This is test-local (NOT part of the `atml`
// library) and is the whole point of the red phase: it is fully working code.
//
// A `DenseRef` is a row-major dense array whose every cell holds the ORIGIN ID
// it came from (its own initial flat index), or `nullopt` for a pad cell. The
// movement ops below transport those ids by pure dense-array rearrangement —
// they NEVER compute a view stride, offset, or mask. Because the `View`'s
// origin buffer is contiguous, a resolved physical flat index equals the
// origin id sitting in the matching oracle cell, so the two can be compared
// cell-for-cell.
//
// INDEPENDENCE RULE: nothing here is shared with `atml::View`'s physical-index
// math. The row-major encode/decode helpers are private dense-STORAGE
// bookkeeping (how a flat std::vector is addressed), not view resolution.
// ===========================================================================
export module test_oracle;

import std;

namespace oracle::detail {
  // Row-major flat address of `idx` within a dense array of `shape`
  // (Horner form). Pure storage addressing for the dense buffer.
  std::int64_t encode(const std::vector<std::int64_t>& idx,
                      const std::vector<std::int64_t>& shape) {
    std::int64_t flat = 0;
    for (std::size_t a = 0; a < shape.size(); a++)
      flat = flat * shape[a] + idx[a];
    return flat;
  }

  // Inverse of encode: the multi-index at row-major position `flat`.
  std::vector<std::int64_t> decode(std::int64_t flat,
                                   const std::vector<std::int64_t>& shape) {
    std::vector<std::int64_t> idx(shape.size(), 0);
    for (std::size_t a = shape.size(); a-- > 0;) {
      idx[a] = flat % shape[a];
      flat /= shape[a];
    }
    return idx;
  }
}

export namespace oracle {
  // An origin id, or `nullopt` for an INVALID/pad cell.
  using Cell = std::optional<std::int64_t>;

  // Product of the extents (empty shape => 1, i.e. a single scalar cell).
  std::int64_t numel(const std::vector<std::int64_t>& shape) {
    std::int64_t n = 1;
    for (const std::int64_t s : shape)
      n *= s;
    return n;
  }

  // Odometer increment of a logical multi-index over `shape`; returns false
  // once it wraps past the last index. Used by tests to walk every logical
  // index — this enumerates LOGICAL indices, it computes no physical address.
  bool next_index(std::vector<std::int64_t>& idx,
                  const std::vector<std::int64_t>& shape) {
    for (std::size_t a = shape.size(); a-- > 0;) {
      if (++idx[a] < shape[a])
        return true;
      idx[a] = 0;
    }
    return false;
  }

  // A dense, row-major reference tensor of origin ids.
  struct DenseRef {
    std::vector<std::int64_t> shape;
    std::vector<Cell> cells;

    const Cell& at(const std::vector<std::int64_t>& idx) const {
      return cells[static_cast<std::size_t>(detail::encode(idx, shape))];
    }
  };

  // Seed: cells[flat] = flat, so each cell's origin id is its own initial
  // row-major flat index.
  DenseRef iota(std::vector<std::int64_t> shape) {
    const std::int64_t n = numel(shape);
    DenseRef ref{std::move(shape), {}};
    ref.cells.reserve(static_cast<std::size_t>(n));
    for (std::int64_t f = 0; f < n; f++)
      ref.cells.push_back(f);
    return ref;
  }

  // Transpose axes: out.shape[i] = in.shape[perm[i]], cells carried across.
  DenseRef permute(const DenseRef& in, std::vector<std::size_t> perm) {
    std::vector<std::int64_t> out_shape(perm.size());
    for (std::size_t i = 0; i < perm.size(); i++)
      out_shape[i] = in.shape[perm[i]];

    const std::int64_t n = numel(out_shape);
    DenseRef out{out_shape, std::vector<Cell>(static_cast<std::size_t>(n))};

    for (std::int64_t f = 0; f < n; f++) {
      const std::vector<std::int64_t> oi = detail::decode(f, out_shape);
      std::vector<std::int64_t> si(in.shape.size());
      for (std::size_t i = 0; i < perm.size(); i++)
        si[perm[i]] = oi[i];
      out.cells[static_cast<std::size_t>(f)] = in.at(si);
    }
    return out;
  }

  // Broadcast: replicate along axes whose old size is 1 (same id copied).
  DenseRef expand(const DenseRef& in, std::vector<std::int64_t> new_shape) {
    const std::int64_t n = numel(new_shape);
    DenseRef out{new_shape, std::vector<Cell>(static_cast<std::size_t>(n))};

    for (std::int64_t f = 0; f < n; f++) {
      const std::vector<std::int64_t> oi = detail::decode(f, new_shape);
      std::vector<std::int64_t> si(in.shape.size());
      for (std::size_t a = 0; a < in.shape.size(); a++)
        si[a] = (in.shape[a] == 1) ? 0 : oi[a];
      out.cells[static_cast<std::size_t>(f)] = in.at(si);
    }
    return out;
  }

  // Slice each axis to [lo, hi).
  DenseRef shrink(const DenseRef& in,
                  std::vector<std::pair<std::int64_t, std::int64_t>> ranges) {
    std::vector<std::int64_t> out_shape(ranges.size());
    for (std::size_t a = 0; a < ranges.size(); a++)
      out_shape[a] = ranges[a].second - ranges[a].first;

    const std::int64_t n = numel(out_shape);
    DenseRef out{out_shape, std::vector<Cell>(static_cast<std::size_t>(n))};

    for (std::int64_t f = 0; f < n; f++) {
      std::vector<std::int64_t> si = detail::decode(f, out_shape);
      for (std::size_t a = 0; a < ranges.size(); a++)
        si[a] += ranges[a].first;
      out.cells[static_cast<std::size_t>(f)] = in.at(si);
    }
    return out;
  }

  // Reverse cell order along each listed axis.
  DenseRef flip(const DenseRef& in, std::vector<std::size_t> axes) {
    std::vector<bool> flipped(in.shape.size(), false);
    for (const std::size_t a : axes)
      flipped[a] = true;

    const std::int64_t n = numel(in.shape);
    DenseRef out{in.shape, std::vector<Cell>(static_cast<std::size_t>(n))};

    for (std::int64_t f = 0; f < n; f++) {
      const std::vector<std::int64_t> oi = detail::decode(f, in.shape);
      std::vector<std::int64_t> si(in.shape.size());
      for (std::size_t a = 0; a < in.shape.size(); a++)
        si[a] = flipped[a] ? in.shape[a] - 1 - oi[a] : oi[a];
      out.cells[static_cast<std::size_t>(f)] = in.at(si);
    }
    return out;
  }

  // Enlarge each axis by (before, after); new border cells are `nullopt`.
  DenseRef pad(const DenseRef& in,
               std::vector<std::pair<std::int64_t, std::int64_t>> pads) {
    std::vector<std::int64_t> out_shape(in.shape.size());
    for (std::size_t a = 0; a < in.shape.size(); a++)
      out_shape[a] = pads[a].first + in.shape[a] + pads[a].second;

    const std::int64_t n = numel(out_shape);
    DenseRef out{out_shape, std::vector<Cell>(static_cast<std::size_t>(n))};

    for (std::int64_t f = 0; f < n; f++) {
      const std::vector<std::int64_t> oi = detail::decode(f, out_shape);
      std::vector<std::int64_t> si(in.shape.size());
      bool inside = true;
      for (std::size_t a = 0; a < in.shape.size(); a++) {
        si[a] = oi[a] - pads[a].first;
        if (si[a] < 0 || si[a] >= in.shape[a])
          inside = false;
      }
      out.cells[static_cast<std::size_t>(f)] =
          inside ? in.at(si) : Cell{std::nullopt};
    }
    return out;
  }

  // Reinterpret the flat cell sequence unchanged into `new_shape` (data order
  // identical); precondition: equal element count.
  DenseRef reshape(const DenseRef& in, std::vector<std::int64_t> new_shape) {
    return DenseRef{std::move(new_shape), in.cells};
  }
}
