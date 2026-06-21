#include <core/assets/AssetRef.h>
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

// A handle-shaped leaf (no TypeSchema) tagged as an asset reference. Stands in
// for StaticMeshHandle/MaterialSetHandle so the reflection test stays in core.
struct FakeHandle { std::uint32_t Index = 0; std::uint32_t Generation = 0; };
struct AssetComp { FakeHandle Mesh; FakeHandle Material; FakeHandle Materials; float Tint = 0.f; };
template <> struct TypeSchema<AssetComp>
{
    static constexpr std::string_view Name = "test.asset";
    static auto Fields()
    {
        return std::tuple{
            MakeField("mesh",      &AssetComp::Mesh).AsAsset(AssetType::StaticMesh),
            MakeField("material",  &AssetComp::Material).AsAsset(AssetType::Material),
            MakeField("materials", &AssetComp::Materials).AsAsset(AssetType::Material, AssetArity::List),
            MakeField("tint",      &AssetComp::Tint),
        };
    }
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

TEST(RuntimeSchema, AssetTaggedFieldsCarryTheirAssetType)
{
    const auto& fields = RuntimeFieldsOf<AssetComp>();
    ASSERT_EQ(fields.size(), 4u);

    const RuntimeField* mesh = Find(fields, "mesh");
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->Asset, AssetType::StaticMesh);
    // A handle is not a scalar the inspector can drag; it is an asset ref instead.
    EXPECT_EQ(mesh->Scalar, FieldScalar::Unsupported);
    // Untagged arity defaults to a single handle.
    EXPECT_EQ(mesh->Arity, AssetArity::Single);

    const RuntimeField* material = Find(fields, "material");
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(material->Asset, AssetType::Material);
    EXPECT_EQ(material->Arity, AssetArity::Single);

    // A list-tagged field keeps its asset type and reflects as an ordered list.
    const RuntimeField* materials = Find(fields, "materials");
    ASSERT_NE(materials, nullptr);
    EXPECT_EQ(materials->Asset, AssetType::Material);
    EXPECT_EQ(materials->Arity, AssetArity::List);

    // An untagged field carries no asset type, so it stays a plain scalar.
    const RuntimeField* tint = Find(fields, "tint");
    ASSERT_NE(tint, nullptr);
    EXPECT_EQ(tint->Asset, AssetType::Unknown);
    EXPECT_EQ(tint->Scalar, FieldScalar::Float);
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
