export module atml.core:sym;

import std;

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

  struct Node;
  using NodePtr = const Node*;
  using Expr = std::tuple<Op, NodePtr, NodePtr>;

  using Payload = std::variant<std::int64_t, std::string_view, Expr>;

  struct Node {
    Payload payload;
    Bound   bound;
  };

  constexpr Bound kFullRange {
    std::numeric_limits<std::int64_t>::min(),
    std::numeric_limits<std::int64_t>::max()
  };

  constexpr Bound add_bounds(const Bound& left, const Bound& right) {
    std::int64_t vmin, vmax;

    if (__builtin_add_overflow(left.vmin, right.vmin, &vmin)) [[unlikely]]
      vmin = std::numeric_limits<std::int64_t>::min();

    if (__builtin_add_overflow(left.vmax, right.vmax, &vmax)) [[unlikely]]
      vmax = std::numeric_limits<std::int64_t>::max();

    return Bound{vmin, vmax};
  }

  constexpr std::int64_t sat_fdiv(std::int64_t left, std::int64_t right) {
    if (right == 0) {
      return (left >= 0) ?
        std::numeric_limits<std::int64_t>::max():
        std::numeric_limits<std::int64_t>::min();
    }

    if (left == std::numeric_limits<std::int64_t>::min() and right == -1)
      return std::numeric_limits<std::int64_t>::max();

    std::int64_t res = left / right;
    std::int64_t rem = left % right;

    if (rem != 0 and ((left ^ right) < 0))
      res--;

    return res;
  }

  constexpr std::int64_t sat_mul(const std::int64_t left, const std::int64_t right) {
    std::int64_t result;

    if (__builtin_mul_overflow(left, right, &result)) [[unlikely]] {
      return ((left > 0) == (right > 0)) ?
        std::numeric_limits<std::int64_t>::max():
        std::numeric_limits<std::int64_t>::min();
    }

    return result;
  }

  constexpr std::int64_t imod(std::int64_t left, std::int64_t right) {
    return left - sat_fdiv(left, right) * right;
  }

  constexpr Bound mul_bounds(const Bound& left, const Bound& right) {
    std::array<std::int64_t, 4> corners {
      sat_mul(left.vmin, right.vmin),
      sat_mul(left.vmin, right.vmax),
      sat_mul(left.vmax, right.vmin),
      sat_mul(left.vmax, right.vmax)
    };

    return Bound{std::ranges::min(corners), std::ranges::max(corners)};
  }

  constexpr Bound fdiv_bounds(const Bound& left, const Bound& right) {
    std::array<std::int64_t, 4> corners {
      sat_fdiv(left.vmin, right.vmin),
      sat_fdiv(left.vmin, right.vmax),
      sat_fdiv(left.vmax, right.vmin),
      sat_fdiv(left.vmax, right.vmax)
    };

    return Bound{std::ranges::min(corners), std::ranges::max(corners)};
  }

  struct Tables {
    std::deque<Node> nodes;
    std::map<std::int64_t, NodePtr> consts;
    std::map<std::string_view, NodePtr, std::less<>> vars;

    /*
     * The DECLARED interval of each variable name, kept separately from
     * Node::bound so re-declaration conflicts can be detected independently of
     * whatever the propagation engine currently stores on the node.
    */

    std::map<std::string_view, Bound, std::less<>> var_decls;
    std::map<Expr, NodePtr> exprs;
  };

  Tables& tables() {
    static Tables t;
    return t;
  }

  NodePtr add_const(std::int64_t val) {
    auto& t = tables();
    auto [itr, inserted] = t.consts.try_emplace(val, nullptr);

    if (inserted) [[likely]]
      itr->second = &t.nodes.emplace_back(Node{Payload{val}, {val, val}});

    return itr->second;
  }

  NodePtr add_var(std::string_view view, Bound decl) {
    auto& t = tables();

    if (decl.vmin > decl.vmax) [[unlikely]]
      throw std::invalid_argument{
        "atml::sym: empty interval " + to_string(decl) + " for variable '"
        + std::string{view} + "' (lo must be <= hi)"};

    auto [ditr, dinserted] = t.var_decls.try_emplace(view, decl);

    if (not dinserted and not (ditr->second == decl)) [[unlikely]]
      throw std::invalid_argument{
        "atml::sym: variable '" + std::string{view} + "' already declared as "
        + to_string(ditr->second) + ", cannot re-declare as " + to_string(decl)};

    auto [itr, inserted] = t.vars.try_emplace(view, nullptr);

    if (inserted) [[likely]]
      itr->second = &t.nodes.emplace_back(Node{Payload{view}, decl});

    return itr->second;
  }

  NodePtr add_var(std::string_view view) {
    return add_var(view, kFullRange);
  }

  constexpr std::int64_t add_op(Op op, NodePtr left, NodePtr right) {
    auto lhs = std::get<std::int64_t>(left->payload);
    auto rhs = std::get<std::int64_t>(right->payload);

    switch (op) {
      case Op::Add: return lhs + rhs;
      case Op::Mul: return lhs * rhs;
      case Op::FDiv: return sat_fdiv(lhs, rhs);
      case Op::Mod: return imod(lhs, rhs);
    }

    return 0;
  }

  NodePtr add_expr(Op op, NodePtr lhs, NodePtr rhs) {
    if (std::get_if<std::int64_t>(&lhs->payload)
        and std::get_if<std::int64_t>(&rhs->payload)) [[unlikely]]
      return add_const(add_op(op, lhs, rhs));

    auto& t = tables();
    Expr expr = {op, lhs, rhs};
    auto [itr, inserted] = t.exprs.try_emplace(expr, nullptr);

    Bound expr_bound = kFullRange;

    switch (op) {
      case Op::Add: expr_bound = add_bounds(lhs->bound, rhs->bound); break;
      case Op::Mul: expr_bound = mul_bounds(lhs->bound, rhs->bound); break;
      case Op::FDiv: expr_bound = fdiv_bounds(lhs->bound, rhs->bound); break;
      case Op::Mod: break;
    }

    if (inserted) [[likely]]
      itr->second = &t.nodes.emplace_back(Node{Payload{expr}, expr_bound});

    return itr->second;
  }
}

export namespace atml {
  // Cheap pointer-sized handle to an interned expression node.
  class Sym {
  public:
    friend bool operator==(const Sym& x, const Sym& y) { return x.node == y.node; }

    Sym(std::int64_t val) : node(atml::intern::add_const(val)) {}
    Sym(std::string_view &var) : node(atml::intern::add_var(var)) {}
    Sym(std::string_view var, std::int64_t lo, std::int64_t hi) :
      node(atml::intern::add_var(var, Bound{lo, hi})) {}
    Sym(atml::intern::Op op, const Sym& lhs, const Sym& rhs) :
      node(atml::intern::add_expr(op, lhs.node, rhs.node)) {}
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

  Sym sym(std::string_view var) { return Sym{var}; }

  Sym sym(std::string_view var, std::int64_t lo, std::int64_t hi) {
    return Sym{var, lo, hi};
  }

  Sym sym_const(std::int64_t val) { return Sym{val}; }

  Bound bounds(const Sym& s) { return s.get_node()->bound; }

  Sym operator+(const Sym& lhs, const Sym& rhs) {
    return Sym{atml::intern::Op::Add, lhs, rhs};
  }

  Sym operator+(const Sym& lhs, std::int64_t rhs) {
    auto rhs_sym = sym_const(rhs);
    return Sym{atml::intern::Op::Add, lhs, rhs_sym};
  }

  Sym operator+(std::int64_t lhs, const Sym& rhs) {
    auto lhs_sym =  sym_const(lhs);
    return Sym{atml::intern::Op::Add, lhs_sym, rhs};
  }

  Sym operator*(const Sym& lhs, const Sym& rhs) {
    return Sym{atml::intern::Op::Mul, lhs, rhs};
  }

  Sym operator*(const Sym& lhs, std::int64_t rhs) {
    auto rhs_sym = sym_const(rhs);
    return Sym{atml::intern::Op::Mul, lhs, rhs_sym};
  }

  Sym operator*(std::int64_t lhs, const Sym& rhs) {
    auto lhs_sym = sym_const(lhs);
    return Sym{atml::intern::Op::Mul, lhs_sym, rhs};
  }

  constexpr Sym floordiv(const Sym& lhs, const Sym& rhs) {
    return Sym{atml::intern::Op::FDiv, lhs, rhs};
  }

  constexpr Sym floordiv(const Sym& lhs, std::int64_t rhs) {
    auto rhs_sym = sym_const(rhs);
    return Sym{atml::intern::Op::FDiv, lhs, rhs_sym};
  }

  constexpr std::int64_t floordiv(std::int64_t lhs, std::int64_t rhs) {
    return atml::intern::sat_fdiv(lhs, rhs);
  }

  constexpr Sym mod(const Sym& lhs, const Sym& rhs) {
    return Sym{atml::intern::Op::Mod, lhs, rhs};
  }

  constexpr Sym mod(const Sym& lhs, std::int64_t rhs) {
    auto rhs_sym = sym_const(rhs);
    return Sym{atml::intern::Op::Mod, lhs, rhs_sym};
  }

  constexpr std::int64_t mod(std::int64_t lhs, std::int64_t rhs) {
    return atml::intern::imod(lhs, rhs);
  }

  std::int64_t eval(const Sym& sym, const Bindings& binds) {
    return std::visit(overloaded {
        [&](std::int64_t val) { return val; },
        [&](std::string_view var) { return binds.find(var)->second; },
        [&](const atml::intern::Expr& expr) {
          auto [op, lhs, rhs] = expr;

          auto left = eval(Sym{lhs}, binds);
          auto right = eval(Sym{rhs}, binds);

          switch (op) {
            case atml::intern::Op::Add: return left + right;
            case atml::intern::Op::Mul: return left * right;
            case atml::intern::Op::FDiv: return floordiv(left, right);
            case atml::intern::Op::Mod: return mod(left, right);
            default: break;
          }

          std::unreachable();
        }
    }, sym.get_node()->payload);
  }

  std::optional<std::int64_t> as_const(const Sym& expr) {
    auto ptr = std::get_if<std::int64_t>(&expr.get_node()->payload);

    if (ptr == nullptr) [[unlikely]]
      return std::nullopt;

    return *ptr;
  }
}
