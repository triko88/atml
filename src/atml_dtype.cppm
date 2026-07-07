export module atml.core:dtype;

import std;

export namespace atml {
  enum class DTypeKind { f32, f16, i8, i4 };

  struct DType {
    DTypeKind kind;

    static DType f32() { return {DTypeKind::f32}; }
    static DType f16() { return {DTypeKind::f16}; }
    static DType i8()  { return {DTypeKind::i8}; }
    static DType i4()  { return {DTypeKind::i4}; }

    constexpr std::size_t bytes() const {
      switch (kind) {
        case DTypeKind::f32: return 4;
        case DTypeKind::f16: return 2;
        case DTypeKind::i8:
        case DTypeKind::i4: return 1;
      }

      return 0;
    }

    constexpr std::string_view name() const {
      switch (kind) {
        case DTypeKind::f32: return "f32";
        case DTypeKind::f16: return "f16";
        case DTypeKind::i8: return "i8";
        case DTypeKind::i4: return "i4";
      }

      return "unset";
    }

    constexpr bool is_float() const {
      return kind == DTypeKind::f32 or kind == DTypeKind::f16;
    }

    bool operator==(const DType&) const = default;
  };

  std::float16_t f16_from_float(float val) {
    return static_cast<std::float16_t>(val);
  }

  float f16_to_float(std::float16_t val) {
    return static_cast<float>(val);
  }
}
