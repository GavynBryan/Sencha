#pragma once

#include <ecs/ComponentTypeId.h>
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

SENCHA_DECLARE_COMPONENT_TYPE(IrradianceVolumeComponent, "IrradianceVolume");
SENCHA_COMPONENT_DECLARES_SCHEMA(IrradianceVolumeComponent);
