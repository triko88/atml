export module atml.core:sym;

import std;

namespace atml::intern {
  enum class Op { Add, Mul, FDiv, Mod };

  struct Node;
  using NodePtr = const Node*;
  using Expr = std::tuple<Op, NodePtr, NodePtr>;

  // Forward declaration for recursive definition
  struct Node : std::variant<std::int64_t, std::string_view, Expr> {
    using variant::variant;
  };

  struct Tables {
    std::deque<Node> nodes;
    std::map<std::int64_t, NodePtr> consts;
    std::map<std::string_view, NodePtr, std::less<>> vars;
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
      itr->second = &t.nodes.emplace_back(Node{val});

    return itr->second;
  }

  NodePtr add_var(std::string_view view) {
    auto& t = tables();
    auto [itr, inserted] = t.vars.try_emplace(view, nullptr);

    if (inserted) [[likely]]
      itr->second = &t.nodes.emplace_back(Node{view});

    return itr->second;
  }

  constexpr std::int64_t ifloordiv(std::int64_t left, std::int64_t right) {
    std::int64_t res = left / right;
    std::int64_t rem = left % right;

    if (rem != 0 and ((left ^ right) < 0))
      res--;

    return res;
  }

  constexpr std::int64_t imod(std::int64_t left, std::int64_t right) {
    return left - ifloordiv(left, right) * right;
  }

  constexpr std::int64_t add_op(Op op, NodePtr left, NodePtr right) {
    auto lhs = std::get<std::int64_t>(*left);
    auto rhs = std::get<std::int64_t>(*right);

    switch (op) {
      case Op::Add: return lhs + rhs;
      case Op::Mul: return lhs * rhs;
      case Op::FDiv: return ifloordiv(lhs, rhs);
      case Op::Mod: return imod(lhs, rhs);
    }

    return 0;
  }

  NodePtr add_expr(Op op, NodePtr lhs, NodePtr rhs) {
    if (std::get_if<std::int64_t>(lhs) and std::get_if<std::int64_t>(rhs)) [[unlikely]]
      return add_const(add_op(op, lhs, rhs));

    auto& t = tables();
    Expr expr = {op, lhs, rhs};
    auto [itr, inserted] = t.exprs.try_emplace(expr, nullptr);

    if (inserted) [[likely]]
      itr->second = &t.nodes.emplace_back(Node{expr});

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

  Sym sym_const(std::int64_t val) { return Sym{val}; }

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
    return atml::intern::ifloordiv(lhs, rhs);
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

  template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
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
    }, *sym.get_node());
  }

  std::optional<std::int64_t> as_const(const Sym& expr) {
    auto ptr = std::get_if<std::int64_t>(expr.get_node());

    if (ptr == nullptr) [[unlikely]]
      return std::nullopt;

    return *ptr;
  }
}
