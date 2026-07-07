import std;
import boost.ut;
import atml.core;

namespace ut = boost::ut;

static ut::suite dtype_suite = [] {
  using namespace ut;
  using namespace atml;

  "traits table"_test = [] {
    expect(DType::f32() == DType::f32());
    expect(DType::f32() != DType::f16());
    expect(DType::i8() != DType::i4());

    expect(DType::f32().bytes() == 4u);
    expect(DType::f16().bytes() == 2u);
    expect(DType::i8().bytes() == 1u);

    expect(DType::f32().name() == "f32");
    expect(DType::f16().name() == "f16");
    expect(DType::i8().name() == "i8");
    expect(DType::i4().name() == "i4");

    expect(DType::f32().is_float());
    expect(DType::f16().is_float());
    expect(not DType::i8().is_float());
    expect(not DType::i4().is_float());
  };

  "i4 storage width is defined"_test = [] {
    // DECISION: i4 reports ceil-to-byte storage — bytes() == 1. True
    // sub-byte packing (two i4 lanes per byte) is deferred; see the skipped
    // test below.
    expect(DType::i4().bytes() == 1u);
  };

  skip / "i4 sub-byte packing"_test = [] {
    // Deferred: a packed-i4 layout needs an element-count-aware size query
    // (e.g. bits() or bytes(count)); revisit when quantized storage lands.
    expect(false);
  };

  "f16 exact round-trip"_test = [] {
    for (const float v : {0.0f, 1.0f, -2.0f, 0.5f, 65504.0f})
      expect(f16_to_float(f16_from_float(v)) == v) << "v=" << v;
  };

  "f16 preserves ordering"_test = [] {
    const std::array sorted{
      -65504.0f, -2.0f, -0.5f, 0.0f, 0.25f, 1.0f, 3.5f, 65504.0f,
    };

    for (std::size_t i = 0; i + 1 < sorted.size(); i++)
      expect(f16_to_float(f16_from_float(sorted[i]))
          < f16_to_float(f16_from_float(sorted[i + 1]))) << "i=" << i;
  };

  "f16 infinity"_test = [] {
    const float inf = std::numeric_limits<float>::infinity();

    expect(f16_to_float(f16_from_float(inf)) == inf);
    expect(f16_to_float(f16_from_float(-inf)) == -inf);

    // DECISION: floats beyond the f16 range overflow to infinity.
    expect(f16_to_float(f16_from_float(1.0e9f)) == inf);
  };

  "f16 subnormal"_test = [] {
    const float smallest = 0x1p-24f;  // smallest positive f16 subnormal

    expect(f16_to_float(f16_from_float(smallest)) == smallest);
    expect(f16_to_float(f16_from_float(-smallest)) == -smallest);
  };
};
