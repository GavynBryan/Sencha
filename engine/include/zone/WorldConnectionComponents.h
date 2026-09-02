#pragma once

#include <core/metadata/EnumSchema.h>
#include <ecs/ComponentAnnotations.h>
#include <math/Vec.h>
#include <world/serialization/SceneFieldCodec.h>
#include <zone/WorldPartitionIds.h>

#include <array>
#include <cstdint>
#include <string_view>

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

// The identities are the schema names, spaces included: they are display
// labels that became persisted identity, and tidying them now would rename
// every component in every cooked world.
struct SENCHA_COMPONENT("World Dock")
       SENCHA_SCHEMA("World Dock")
       SENCHA_SCENE_CHUNK("WDCK")
WorldDock
{
    SENCHA_FIELD("id")
    DockId Id;

    SENCHA_FIELD("zone_a")
    ZoneId ZoneA;

    SENCHA_FIELD("zone_b")
    ZoneId ZoneB;

    SENCHA_FIELD("half_extents")
    Vec2d HalfExtents{ 1.0f, 1.5f };

    SENCHA_FIELD("directions")
    std::uint32_t Directions = DockDirectionBoth;
};

struct SENCHA_COMPONENT("World Link")
       SENCHA_SCHEMA("World Link")
       SENCHA_SCENE_CHUNK("WLNK")
WorldLink
{
    SENCHA_FIELD("id")
    LinkId Id;

    SENCHA_FIELD("zone_a")
    ZoneId ZoneA;

    SENCHA_FIELD("zone_b")
    ZoneId ZoneB;

    SENCHA_FIELD("kind")
    LinkKind Kind = LinkKind::Teleport;

    SENCHA_FIELD("directions")
    std::uint32_t Directions = DockDirectionBoth;
};

struct SENCHA_COMPONENT("Dock Gate Binding")
       SENCHA_SCHEMA("Dock Gate Binding")
       SENCHA_SCENE_CHUNK("DGAT")
DockGateBinding
{
    SENCHA_FIELD("dock")
    DockId Id;
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

#if !defined(SENCHA_CODEGEN)
#  include <zone/WorldConnectionComponents.sencha.h>
#endif
