#pragma once

#include <ecs/ComponentAnnotations.h>
#include <math/Vec.h>

#include <cstdint>

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
struct SENCHA_COMPONENT("IrradianceVolume")
       SENCHA_SCHEMA("IrradianceVolume")
       SENCHA_SCENE_CHUNK("IRVL")
IrradianceVolumeComponent
{
    SENCHA_FIELD("half_extents")
    Vec3d HalfExtents = Vec3d(4.0f, 2.0f, 4.0f);

    // World units between probes. Authoring guidance: keep at or above
    // interior wall thickness so dilation cannot bleed across a thin wall.
    SENCHA_FIELD("cell_size")
    float CellSize = 1.0f;

    SENCHA_FIELD("priority")
    std::int32_t Priority = 0;
};

#if !defined(SENCHA_CODEGEN)
#  include <render/IrradianceVolumeComponent.sencha.h>
#endif
