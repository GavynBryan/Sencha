#pragma once

#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <math/MathSchemas.h>
#include <world/serialization/SceneFieldCodec.h>
#include <zone/WorldPartitionIds.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <tuple>

enum class LinkKind : std::uint32_t
{
    Teleport,
};

template <>
struct EnumSchema<LinkKind>
{
    static constexpr std::array Values = {
        EnumValue{ LinkKind::Teleport, "teleport" },
    };
};

enum DockDirection : std::uint32_t
{
    DockDirectionAToB = 1u << 0,
    DockDirectionBToA = 1u << 1,
    DockDirectionBoth = DockDirectionAToB | DockDirectionBToA,
};

struct WorldDock
{
    DockId Id;
    ZoneId ZoneA;
    ZoneId ZoneB;
    Vec2d HalfExtents{ 1.0f, 1.5f };
    std::uint32_t Directions = DockDirectionBoth;
};

struct WorldLink
{
    LinkId Id;
    ZoneId ZoneA;
    ZoneId ZoneB;
    LinkKind Kind = LinkKind::Teleport;
    std::uint32_t Directions = DockDirectionBoth;
};

struct DockGateBinding
{
    DockId Id;
};

template <>
struct TypeSchema<WorldDock>
{
    static constexpr std::string_view Name = "World Dock";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('W', 'D', 'C', 'K');

    static auto Fields()
    {
        return std::tuple{
            MakeField("id", &WorldDock::Id),
            MakeField("zone_a", &WorldDock::ZoneA),
            MakeField("zone_b", &WorldDock::ZoneB),
            MakeField("half_extents", &WorldDock::HalfExtents),
            MakeField("directions", &WorldDock::Directions),
        };
    }
};

template <>
struct TypeSchema<WorldLink>
{
    static constexpr std::string_view Name = "World Link";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('W', 'L', 'N', 'K');

    static auto Fields()
    {
        return std::tuple{
            MakeField("id", &WorldLink::Id),
            MakeField("zone_a", &WorldLink::ZoneA),
            MakeField("zone_b", &WorldLink::ZoneB),
            MakeField("kind", &WorldLink::Kind),
            MakeField("directions", &WorldLink::Directions),
        };
    }
};

template <>
struct TypeSchema<DockGateBinding>
{
    static constexpr std::string_view Name = "Dock Gate Binding";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('D', 'G', 'A', 'T');

    static auto Fields()
    {
        return std::tuple{
            MakeField("dock", &DockGateBinding::Id),
        };
    }
};

template <>
struct SceneFieldCodec<ZoneId>
{
    static bool Save(IWriteArchive&, std::string_view, ZoneId,
                     SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, ZoneId&,
                     SceneSerializationContext&);
};

template <>
struct SceneFieldCodec<DockId>
{
    static bool Save(IWriteArchive&, std::string_view, DockId,
                     SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, DockId&,
                     SceneSerializationContext&);
};

template <>
struct SceneFieldCodec<LinkId>
{
    static bool Save(IWriteArchive&, std::string_view, LinkId,
                     SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, LinkId&,
                     SceneSerializationContext&);
};

// Stated rather than derived from TypeSchema::Name, so the schemas can move
// without the identities moving with them. The names are repeated exactly,
// spaces included: they are display labels that became persisted identity, and
// tidying them now would rename every component in every cooked world.
SENCHA_DECLARE_COMPONENT_TYPE(WorldDock,       "World Dock");
SENCHA_DECLARE_COMPONENT_TYPE(WorldLink,       "World Link");
SENCHA_DECLARE_COMPONENT_TYPE(DockGateBinding, "Dock Gate Binding");
