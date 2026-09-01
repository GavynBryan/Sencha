#pragma once

#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <math/MathSchemas.h>
#include <world/serialization/SceneFieldCodec.h>
#include <zone/WorldConnectionComponents.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape for the world-connection components.
//
// Registration and the serializers include this; the systems that read these
// components do not.
//=============================================================================

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
