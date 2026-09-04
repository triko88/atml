// ===========================================================================
// atml_sym.cppm — interface of the atml.core:sym partition
//
// This unit declares the symbolic engine's surface: the Bound interval, the
// pointer-sized Sym handle, and the operations over it.
//
// Everything behind that surface -- the intern tables, the Node payload and
// the interval-propagation arithmetic -- is implementation detail and lives in
// src/atml_sym.cpp, a module implementation unit of atml.core. Node is left
// incomplete here on purpose: Sym only ever stores a pointer to one, so the
// layout of the graph can change without recompiling consumers.
//
// The two exceptions are sat_fdiv/imod: the exported constexpr integer
// overloads of floordiv/mod are usable in constant expressions, so the
// definitions they call must be reachable from this interface.
// ===========================================================================

export module atml.core:sym;

import std;

// Visitor helper for std::visit. Not exported, but reachable from every unit
// of atml.core that imports this partition -- :dim relies on that.
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

export namespace atml {
  struct Bound {
    std::int64_t vmin;
    std::int64_t vmax;
    friend bool operator==(const Bound&, const Bound&) = default;
  };

  constexpr std::string to_string(const Bound& b) {
    return "[" + std::to_string(b.vmin) + ", " + std::to_string(b.vmax) + "]";
  }
}

namespace atml::intern {
  enum class Op { Add, Mul, FDiv, Mod };

  // Opaque: defined in atml_sym.cpp.
  struct Node;
  using NodePtr = const Node*;

  constexpr std::int64_t sat_fdiv(std::int64_t left, std::int64_t right) {
    if (right == 0) [[unlikely]] {
      return (left < 0) ?
        std::numeric_limits<std::int64_t>::min():
        std::numeric_limits<std::int64_t>::max();
    }

    if (left == std::numeric_limits<std::int64_t>::min() and right == -1)
      return std::numeric_limits<std::int64_t>::max();

    bool is_negative = (left < 0) ^ (right < 0);
    bool has_remainder = (left % right != 0);

    if (is_negative and has_remainder) {
      left = std::saturating_sub(left, (right > 0) ?
        std::saturating_sub(right, std::int64_t{1}):
        std::saturating_add(right, std::int64_t{1}));
    }

    return std::saturating_div(left, right);
  }

  constexpr std::int64_t imod(std::int64_t left, std::int64_t right) {
    return left - sat_fdiv(left, right) * right;
  }

  // Interning entry points -- defined in atml_sym.cpp.
  NodePtr add_const(std::int64_t val);
  NodePtr add_var(std::string_view view, Bound decl);
  NodePtr add_var(std::string_view view);
  NodePtr add_expr(Op op, NodePtr lhs, NodePtr rhs);
}

export namespace atml {
  // Cheap pointer-sized handle to an interned expression node.
  class Sym {
  public:
    friend bool operator==(const Sym& x, const Sym& y) { return x.node == y.node; }

    Sym(std::int64_t val);
    Sym(std::string_view &var);
    Sym(std::string_view var, std::int64_t lo, std::int64_t hi);
    Sym(atml::intern::Op op, const Sym& lhs, const Sym& rhs);
    Sym(atml::intern::NodePtr node) : node(node) {}

    atml::intern::NodePtr get_node() const {
      return this->node;
    }

  private:
    atml::intern::NodePtr node = nullptr;
  };

  // DECISION: ordered map with transparent comparator so string_view names
  // can be looked up without constructing a std::string.
  using Bindings = std::map<std::string, std::int64_t, std::less<>>;

  Sym sym(std::string_view var);
  Sym sym(std::string_view var, std::int64_t lo, std::int64_t hi);
  Sym sym_const(std::int64_t val);

  Bound bounds(const Sym& s);

  Sym operator+(const Sym& lhs, const Sym& rhs);
  Sym operator+(const Sym& lhs, std::int64_t rhs);
  Sym operator+(std::int64_t lhs, const Sym& rhs);

  Sym operator*(const Sym& lhs, const Sym& rhs);
  Sym operator*(const Sym& lhs, std::int64_t rhs);
  Sym operator*(std::int64_t lhs, const Sym& rhs);

  // The Sym overloads build interned nodes, so they can never be constant
  // evaluated; only the integer overloads stay constexpr.
  Sym floordiv(const Sym& lhs, const Sym& rhs);
  Sym floordiv(const Sym& lhs, std::int64_t rhs);

  constexpr std::int64_t floordiv(std::int64_t lhs, std::int64_t rhs) {
    return atml::intern::sat_fdiv(lhs, rhs);
  }

  Sym mod(const Sym& lhs, const Sym& rhs);
  Sym mod(const Sym& lhs, std::int64_t rhs);

  constexpr std::int64_t mod(std::int64_t lhs, std::int64_t rhs) {
    return atml::intern::imod(lhs, rhs);
  }

  std::int64_t eval(const Sym& sym, const Bindings& binds);

  std::optional<std::int64_t> as_const(const Sym& expr);
}
