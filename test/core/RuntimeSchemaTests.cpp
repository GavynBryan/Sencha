#include <core/metadata/Field.h>
#include <core/metadata/RuntimeSchema.h>
#include <math/MathSchemas.h>
#include <math/Vec.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <string_view>
#include <tuple>

// ─── Test components ─────────────────────────────────────────────────────────

struct FlatComp { bool Flag = false; std::int32_t Count = 0; float Speed = 0.f; double Mass = 0.0; };
template <> struct TypeSchema<FlatComp>
{
    static constexpr std::string_view Name = "test.flat";
    static auto Fields()
    {
        return std::tuple{
            MakeField("flag",  &FlatComp::Flag),
            MakeField("count", &FlatComp::Count),
            MakeField("speed", &FlatComp::Speed),
            MakeField("mass",  &FlatComp::Mass),
        };
    }
};

struct NestedComp { Vec3d Position; float Radius = 0.f; };
template <> struct TypeSchema<NestedComp>
{
    static constexpr std::string_view Name = "test.nested";
    static auto Fields()
    {
        return std::tuple{
            MakeField("position", &NestedComp::Position),
            MakeField("radius",   &NestedComp::Radius),
        };
    }
};

enum class Mode : std::uint8_t { A = 0, B = 1, C = 2 };
struct EnumComp { Mode M = Mode::A; };
template <> struct TypeSchema<EnumComp>
{
    static constexpr std::string_view Name = "test.enum";
    static auto Fields() { return std::tuple{ MakeField("mode", &EnumComp::M) }; }
};

namespace
{
    const RuntimeField* Find(const std::vector<RuntimeField>& fields, std::string_view name)
    {
        for (const auto& f : fields)
            if (f.Name == name) return &f;
        return nullptr;
    }
}

TEST(RuntimeSchema, FlatScalarsHaveCorrectKindsAndSizes)
{
    const auto& fields = RuntimeFieldsOf<FlatComp>();
    ASSERT_EQ(fields.size(), 4u);

    const RuntimeField* flag = Find(fields, "flag");
    ASSERT_NE(flag, nullptr);
    EXPECT_EQ(flag->Scalar, FieldScalar::Bool);

    EXPECT_EQ(Find(fields, "count")->Scalar, FieldScalar::Int32);
    EXPECT_EQ(Find(fields, "speed")->Scalar, FieldScalar::Float);
    EXPECT_EQ(Find(fields, "mass")->Scalar, FieldScalar::Double);
    EXPECT_EQ(Find(fields, "mass")->Size, sizeof(double));
}

TEST(RuntimeSchema, OffsetsRoundTripThroughRawBytes)
{
    const auto& fields = RuntimeFieldsOf<FlatComp>();
    FlatComp c{};
    auto* base = reinterpret_cast<std::byte*>(&c);

    // Writing at the descriptor's offset must land in the named member.
    const RuntimeField* speed = Find(fields, "speed");
    ASSERT_NE(speed, nullptr);
    const float newSpeed = 12.5f;
    std::memcpy(base + speed->Offset, &newSpeed, speed->Size);
    EXPECT_FLOAT_EQ(c.Speed, 12.5f);

    const RuntimeField* count = Find(fields, "count");
    const std::int32_t newCount = 99;
    std::memcpy(base + count->Offset, &newCount, count->Size);
    EXPECT_EQ(c.Count, 99);
}

TEST(RuntimeSchema, NestedSchemasFlattenWithDottedPaths)
{
    const auto& fields = RuntimeFieldsOf<NestedComp>();
    // position -> x,y,z (3 doubles) + radius
    ASSERT_EQ(fields.size(), 4u);

    // Vec3d == Vec<3> whose scalar is float in this codebase, so leaves are Float.
    const RuntimeField* py = Find(fields, "position.y");
    ASSERT_NE(py, nullptr);
    EXPECT_EQ(py->Scalar, FieldScalar::Float);

    NestedComp n{};
    auto* base = reinterpret_cast<std::byte*>(&n);
    const float v = 3.25f;
    std::memcpy(base + py->Offset, &v, py->Size);
    EXPECT_FLOAT_EQ(n.Position.Y, 3.25f);

    const RuntimeField* radius = Find(fields, "radius");
    ASSERT_NE(radius, nullptr);
    EXPECT_EQ(radius->Scalar, FieldScalar::Float);
    const float r = 7.0f;
    std::memcpy(base + radius->Offset, &r, radius->Size);
    EXPECT_FLOAT_EQ(n.Radius, 7.0f);
}

TEST(RuntimeSchema, EnumMapsToUnderlyingKindAndSize)
{
    const auto& fields = RuntimeFieldsOf<EnumComp>();
    ASSERT_EQ(fields.size(), 1u);
    const RuntimeField* mode = Find(fields, "mode");
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->Scalar, FieldScalar::UInt32); // underlying is unsigned
    EXPECT_EQ(mode->Size, sizeof(Mode));          // but only one byte wide
}
