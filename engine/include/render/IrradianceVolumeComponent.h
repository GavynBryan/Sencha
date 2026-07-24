#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <math/Vec.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// IrradianceVolumeComponent
//
// An authored baked-irradiance probe volume: an axis-aligned box of probes
// centered on the entity's transform, sampled per fragment at runtime in
// place of the hemispheric ambient. The cook grows a probe lattice over the
// box at CellSize spacing and bakes it into the zone's .sprobe payload.
// Priority resolves overlaps: higher wins, then the smaller volume, so a
// deliberate local override can sit inside a room volume.
//=============================================================================
struct IrradianceVolumeComponent
{
    Vec3d HalfExtents = Vec3d(4.0f, 2.0f, 4.0f);
    // World units between probes. Authoring guidance: keep at or above
    // interior wall thickness so dilation cannot bleed across a thin wall.
    float CellSize = 1.0f;
    std::int32_t Priority = 0;
};

template <>
struct TypeSchema<IrradianceVolumeComponent>
{
    static constexpr std::string_view Name = "IrradianceVolume";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('I', 'R', 'V', 'L');

    static auto Fields()
    {
        const IrradianceVolumeComponent defaults{};
        return std::tuple{
            MakeField("half_extents", &IrradianceVolumeComponent::HalfExtents)
                .Default(defaults.HalfExtents),
            MakeField("cell_size", &IrradianceVolumeComponent::CellSize)
                .Default(defaults.CellSize),
            MakeField("priority", &IrradianceVolumeComponent::Priority)
                .Default(defaults.Priority),
        };
    }
};
