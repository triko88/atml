export module atml.core:dim;

import :sym;
import std;

export namespace atml {
  using Dim = std::variant<std::int64_t, Sym>;

  // struct overloaded => defined in atml_sym.cppm
  Dim operator + (const Dim& lhs, const Dim& rhs) {
    return std::visit(overloaded{
        [](std::int64_t x, std::int64_t y) -> Dim{ return x + y; },
        [](auto&& x, auto&& y) -> Dim{ return Sym{x} + Sym{y}; }
        }, lhs, rhs);
  }

  Dim& operator += (Dim& lhs, const std::int64_t rhs) {
    lhs = lhs + rhs;
    return lhs;
  }

  Dim& operator += (Dim& lhs, const Dim& rhs) {
    lhs = lhs + rhs;
    return lhs;
  }

  Dim operator * (const Dim& lhs, const Dim& rhs) {
    return std::visit(overloaded{
        [](std::int64_t x, std::int64_t y) -> Dim{ return x * y; },
        [](auto&& x, auto&& y) -> Dim{ return Sym{x} * Sym{y}; }
        }, lhs, rhs);
  }

  Dim& operator *= (Dim& lhs, const std::int64_t rhs) {
    lhs = lhs * rhs;
    return lhs;
  }

  Dim& operator *= (Dim& lhs, const Dim& rhs) {
    lhs = lhs * rhs;
    return lhs;
  }

  Dim operator - (const Dim& lhs, const Dim& rhs) {
    return lhs + (rhs * -1);
  }

  Dim& operator -= (Dim& lhs, Dim& rhs) {
    lhs = lhs + (rhs * -1);
    return lhs;
  }

  Dim floordiv(const Dim& lhs, const Dim& rhs) {
    return std::visit(overloaded{
        [](std::int64_t x, std::int64_t y) -> Dim{ return floordiv(x, y); },
        [](auto&& x, auto&& y) -> Dim{ return floordiv(x, y); },
      }, lhs, rhs);
  }
  Dim mod(const Dim& lhs, const Dim& rhs) {
    return std::visit(overloaded{
        [](std::int64_t x, std::int64_t y) -> Dim { return mod(x, y); },
        [](auto&& x, auto&& y) -> Dim{ return mod(x, y); }
    }, lhs, rhs);
  }

  std::optional<bool> dim_eq(const Dim& lhs, const Dim& rhs) {
    return std::visit(overloaded{
        [](std::int64_t x, std::int64_t y) -> std::optional<bool> { 
          return x == y;
        },
        [](const Sym& x, const Sym& y) -> std::optional<bool> {
          if (x == y)
            return true;
          return std::nullopt;
        },
        [](auto&&, auto&&) -> std::optional<bool> {
          return std::nullopt;
        }
    }, lhs, rhs);
  }

  std::optional<bool> dim_lt(const Dim& lhs, const Dim& rhs) {
    return std::visit(overloaded{
        [](std::int64_t x, std::int64_t y) -> std::optional<bool> { 
          return x < y;
        },
        [](auto&& x, auto&& y) -> std::optional<bool> {
          auto x_i64 = as_const(x);
          auto y_i64 = as_const(y);

          if(not (x_i64.has_value() and y_i64.has_value()))
            return std::nullopt;

          return x_i64.value() < y_i64.value();
        }
    }, lhs, rhs);
  }

  std::int64_t dim_eval(const Dim& dim, const Bindings& binds) {
    return std::visit(overloaded{
        [](std::int64_t x) { return x; },
        [&](const Sym& sym) { return eval(sym, binds); }
    }, dim);
  }
}
