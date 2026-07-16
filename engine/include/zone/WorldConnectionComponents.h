#pragma once

#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <core/text/InlineString.h>
#include <math/MathSchemas.h>
#include <math/geometry/3d/Aabb3d.h>
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

using WorldDemandCondition = InlineString<128>;

struct WorldDock
{
    DockId Id;
    ZoneId ZoneA;
    ZoneId ZoneB;
    Vec2d HalfExtents{ 1.0f, 1.5f };
    Aabb3d SideAArmBounds{ { -1.0f, -1.5f, -2.0f }, { 1.0f, 1.5f, 0.0f } };
    Aabb3d SideBArmBounds{ { -1.0f, -1.5f, 0.0f }, { 1.0f, 1.5f, 2.0f } };
    std::uint32_t Directions = DockDirectionBoth;
    std::int32_t PreloadPriority = 0;
    std::int32_t PreloadDepth = 0;
    WorldDemandCondition DemandCondition;
};

struct WorldLink
{
    LinkId Id;
    ZoneId ZoneA;
    ZoneId ZoneB;
    LinkKind Kind = LinkKind::Teleport;
    std::uint32_t Directions = DockDirectionBoth;
    std::int32_t PreloadPriority = 0;
    std::int32_t PreloadDepth = 0;
    WorldDemandCondition DemandCondition;
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
            MakeField("side_a_arm_bounds", &WorldDock::SideAArmBounds),
            MakeField("side_b_arm_bounds", &WorldDock::SideBArmBounds),
            MakeField("directions", &WorldDock::Directions),
            MakeField("preload_priority", &WorldDock::PreloadPriority),
            MakeField("preload_depth", &WorldDock::PreloadDepth),
            MakeField("demand_condition", &WorldDock::DemandCondition),
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
            MakeField("preload_priority", &WorldLink::PreloadPriority),
            MakeField("preload_depth", &WorldLink::PreloadDepth),
            MakeField("demand_condition", &WorldLink::DemandCondition),
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
