#include <core/assets/AssetRef.h>
#include <core/identity/StrongId.h>
#include <core/metadata/Field.h>
#include <core/metadata/RuntimeSchema.h>
#include <math/MathSchemas.h>
#include <math/Quat.h>
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

// A color-tagged Vec3 collapses to one Color3 leaf; an untagged Vec3 still
// flattens to x/y/z. Tint stands in for a non-color trailing scalar.
struct ColorComp { Vec<3> Color; Vec<3> Plain; float Tint = 0.f; };
template <> struct TypeSchema<ColorComp>
{
    static constexpr std::string_view Name = "test.color";
    static auto Fields()
    {
        return std::tuple{
            MakeField("color", &ColorComp::Color).AsColor(),
            MakeField("plain", &ColorComp::Plain),
            MakeField("tint",  &ColorComp::Tint),
        };
    }
};

// A transform-like component: Vec3/Quat members group into one N-wide leaf each
// instead of flattening to x/y/z/w.
struct GroupedComp { Vec3d Position; Quat<float> Rotation; Vec3d Scale; };
template <> struct TypeSchema<GroupedComp>
{
    static constexpr std::string_view Name = "test.grouped";
    static auto Fields()
    {
        return std::tuple{
            MakeField("position", &GroupedComp::Position),
            MakeField("rotation", &GroupedComp::Rotation),
            MakeField("scale",    &GroupedComp::Scale),
        };
    }
};

using TestId = StrongId<struct TestIdTag, std::uint32_t>;
struct IdComp { TestId Id; float Value = 0.f; };
template <> struct TypeSchema<IdComp>
{
    static constexpr std::string_view Name = "test.id";
    static auto Fields()
    {
        return std::tuple{
            MakeField("id",    &IdComp::Id),
            MakeField("value", &IdComp::Value),
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

TEST(RuntimeSchema, ColorTaggedVec3CollapsesToOneColor3Leaf)
{
    const auto& fields = RuntimeFieldsOf<ColorComp>();
    // color -> 1 Color3 leaf; plain -> 1 grouped Vec3 leaf; tint -> 1. = 3 leaves.
    ASSERT_EQ(fields.size(), 3u);

    const RuntimeField* color = Find(fields, "color");
    ASSERT_NE(color, nullptr);
    EXPECT_EQ(color->Scalar, FieldScalar::Color3);
    EXPECT_EQ(color->Size, 3 * sizeof(float));
    EXPECT_EQ(color->Asset, AssetType::Unknown);

    // The untagged Vec3 groups into one N-wide leaf; it does not flatten to x/y/z.
    EXPECT_EQ(Find(fields, "color.x"), nullptr);
    EXPECT_EQ(Find(fields, "plain.x"), nullptr);
    const RuntimeField* plain = Find(fields, "plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(plain->Scalar, FieldScalar::Float);
    EXPECT_EQ(plain->Count, 3u);

    // The Color3 leaf addresses the real bytes: writing channel 1 hits Color.Y.
    ColorComp c{};
    auto* base = reinterpret_cast<std::byte*>(&c);
    const float green = 0.5f;
    std::memcpy(base + color->Offset + sizeof(float), &green, sizeof(float));
    EXPECT_FLOAT_EQ(c.Color.Y, 0.5f);
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

TEST(RuntimeSchema, NestedVectorGroupsIntoOneLeaf)
{
    const auto& fields = RuntimeFieldsOf<NestedComp>();
    // position -> 1 grouped Vec3 leaf + radius
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(Find(fields, "position.y"), nullptr);

    // Vec3d == Vec<3> whose scalar is float in this codebase, so the leaf is Float.
    const RuntimeField* position = Find(fields, "position");
    ASSERT_NE(position, nullptr);
    EXPECT_EQ(position->Scalar, FieldScalar::Float);
    EXPECT_EQ(position->Count, 3u);
    EXPECT_EQ(position->Size, sizeof(float));

    NestedComp n{};
    auto* base = reinterpret_cast<std::byte*>(&n);
    // The grouped leaf addresses contiguous components: channel 1 hits Position.Y.
    const float v = 3.25f;
    std::memcpy(base + position->Offset + position->Size, &v, position->Size);
    EXPECT_FLOAT_EQ(n.Position.Y, 3.25f);

    const RuntimeField* radius = Find(fields, "radius");
    ASSERT_NE(radius, nullptr);
    EXPECT_EQ(radius->Scalar, FieldScalar::Float);
    EXPECT_EQ(radius->Count, 1u);
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

TEST(RuntimeSchema, VecAndQuatGroupIntoOneLeafEach)
{
    const auto& fields = RuntimeFieldsOf<GroupedComp>();
    // position + rotation + scale, each one grouped leaf.
    ASSERT_EQ(fields.size(), 3u);

    const RuntimeField* position = Find(fields, "position");
    ASSERT_NE(position, nullptr);
    EXPECT_EQ(position->Scalar, FieldScalar::Float);
    EXPECT_EQ(position->Count, 3u);
    EXPECT_EQ(position->Offset, offsetof(GroupedComp, Position));

    const RuntimeField* rotation = Find(fields, "rotation");
    ASSERT_NE(rotation, nullptr);
    EXPECT_EQ(rotation->Scalar, FieldScalar::Float);
    EXPECT_EQ(rotation->Count, 4u); // a quaternion groups all four components
    EXPECT_EQ(rotation->Offset, offsetof(GroupedComp, Rotation));

    const RuntimeField* scale = Find(fields, "scale");
    ASSERT_NE(scale, nullptr);
    EXPECT_EQ(scale->Count, 3u);
    EXPECT_EQ(scale->Offset, offsetof(GroupedComp, Scale));

    // The grouped leaves address contiguous components at Offset + i*Size.
    GroupedComp g{};
    auto* base = reinterpret_cast<std::byte*>(&g);
    const float rz = 0.75f;
    std::memcpy(base + rotation->Offset + 2 * rotation->Size, &rz, rotation->Size);
    EXPECT_FLOAT_EQ(g.Rotation.Z, 0.75f);
    const float sx = 2.0f;
    std::memcpy(base + scale->Offset, &sx, scale->Size);
    EXPECT_FLOAT_EQ(g.Scale.X, 2.0f);
}

TEST(RuntimeSchema, StrongIdReflectsAsReadOnlyUnderlyingScalar)
{
    const auto& fields = RuntimeFieldsOf<IdComp>();
    ASSERT_EQ(fields.size(), 2u);

    const RuntimeField* id = Find(fields, "id");
    ASSERT_NE(id, nullptr);
    // The id surfaces its underlying integer, shown but not editable.
    EXPECT_EQ(id->Scalar, FieldScalar::UInt32);
    EXPECT_EQ(id->Size, sizeof(std::uint32_t));
    EXPECT_EQ(id->Count, 1u);
    EXPECT_TRUE(id->ReadOnly);
    EXPECT_EQ(id->Asset, AssetType::Unknown);

    // The leaf addresses the StrongId's underlying value.
    IdComp c{};
    auto* base = reinterpret_cast<std::byte*>(&c);
    const std::uint32_t raw = 42;
    std::memcpy(base + id->Offset, &raw, id->Size);
    EXPECT_EQ(c.Id.Value, 42u);

    // A plain scalar sibling stays editable.
    const RuntimeField* value = Find(fields, "value");
    ASSERT_NE(value, nullptr);
    EXPECT_FALSE(value->ReadOnly);
}
