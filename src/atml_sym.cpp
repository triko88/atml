// ===========================================================================
// atml_sym.cpp — implementation of the symbolic engine declared in
// src/atml_sym.cppm (the atml.core:sym partition).
//
// A module implementation unit of atml.core, so everything defined here is
// attached to the module: the intern tables, the Node representation and the
// interval arithmetic stay invisible to consumers, which only ever see the
// declarations in the partition interface.
// ===========================================================================

module atml.core;

import :sym;
import std;

namespace atml::intern {
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

  constexpr Bound kInvertRange {
    std::numeric_limits<std::int64_t>::max(),
    std::numeric_limits<std::int64_t>::min()
  };

  constexpr Bound add_bounds(const Bound& left, const Bound& right) {
    return Bound {
      std::saturating_add(left.vmin, right.vmin),
      std::saturating_add(left.vmax, right.vmax)
    };
  }

  constexpr Bound sub_bounds(const Bound& left, const Bound& right) {
    return Bound{
      std::saturating_sub(left.vmin, right.vmin),
      std::saturating_sub(left.vmax, right.vmax)
    };
  }

  constexpr Bound mul_bounds(const Bound& left, const Bound& right) {
    std::array<std::int64_t, 4> corners {
      std::saturating_mul(left.vmin, right.vmin),
      std::saturating_mul(left.vmin, right.vmax),
      std::saturating_mul(left.vmax, right.vmin),
      std::saturating_mul(left.vmax, right.vmax)
    };

    return Bound{std::ranges::min(corners), std::ranges::max(corners)};
  }

  constexpr Bound fdiv_bounds(const Bound& left, const Bound& right) {
    if (right.vmin <= 0 and right.vmax >= 0)
      return kFullRange;

    std::array<std::int64_t, 4> corners {
      sat_fdiv(left.vmin, right.vmin),
      sat_fdiv(left.vmin, right.vmax),
      sat_fdiv(left.vmax, right.vmin),
      sat_fdiv(left.vmax, right.vmax)
    };

    return Bound{std::ranges::min(corners), std::ranges::max(corners)};
  }

  constexpr Bound meet(const Bound& left, const Bound& right) {
    return Bound{ std::max(left.vmin, right.vmin), std::min(left.vmax, right.vmax) };
  }

  constexpr Bound join(const Bound& left, const Bound& right) {
    return Bound{ std::min(left.vmin, right.vmin), std::max(left.vmax, right.vmax) };
  }

  constexpr Bound mod_half(const Bound& left, const Bound& right) {
    if (right.vmin > right.vmax)
      return kInvertRange;

    const bool pos = right.vmin >= 0;
    const bool is_const = right.vmin == right.vmax;

    const bool in_window = is_const and
      (pos ? (left.vmin >= 0 and left.vmax < right.vmin):
       (left.vmax <= 0 and left.vmin > right.vmax));

    const Bound general = pos
      ? Bound{0, std::saturating_sub(right.vmax, std::int64_t{1})}:
      Bound{std::saturating_sub(right.vmin, std::int64_t{-1}), 0};

    return in_window ? meet(general, left) : general;
  }

  constexpr Bound mod_bounds(const Bound& left, const Bound& right) {
    if (right.vmin <= 0 and right.vmax >= 0)
      return kFullRange;

    const Bound kPosHalf = Bound{std::max(right.vmin, std::int64_t{1}), right.vmax};
    const Bound kNegHalf = Bound{right.vmin, std::min(std::int64_t{-1}, right.vmax)};

    return join(mod_half(left, kPosHalf), mod_half(left, kNegHalf));
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
      case Op::Mod: expr_bound = mod_bounds(lhs->bound, rhs->bound); break;
    }

    if (inserted) [[likely]]
      itr->second = &t.nodes.emplace_back(Node{Payload{expr}, expr_bound});

    return itr->second;
  }
}

namespace atml {
  Sym::Sym(std::int64_t val) : node(atml::intern::add_const(val)) {}

  Sym::Sym(std::string_view &var) : node(atml::intern::add_var(var)) {}

  Sym::Sym(std::string_view var, std::int64_t lo, std::int64_t hi) :
    node(atml::intern::add_var(var, Bound{lo, hi})) {}

  Sym::Sym(atml::intern::Op op, const Sym& lhs, const Sym& rhs) :
    node(atml::intern::add_expr(op, lhs.node, rhs.node)) {}

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

  Sym floordiv(const Sym& lhs, const Sym& rhs) {
    return Sym{atml::intern::Op::FDiv, lhs, rhs};
  }

  Sym floordiv(const Sym& lhs, std::int64_t rhs) {
    auto rhs_sym = sym_const(rhs);
    return Sym{atml::intern::Op::FDiv, lhs, rhs_sym};
  }

  Sym mod(const Sym& lhs, const Sym& rhs) {
    return Sym{atml::intern::Op::Mod, lhs, rhs};
  }

  Sym mod(const Sym& lhs, std::int64_t rhs) {
    auto rhs_sym = sym_const(rhs);
    return Sym{atml::intern::Op::Mod, lhs, rhs_sym};
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
