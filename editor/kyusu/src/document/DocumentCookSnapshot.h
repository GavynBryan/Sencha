#pragma once

#include "BrushCookInput.h"

#include <assets/cook/DirectLightBake.h>
#include <assets/cook/LightingCookParams.h>
#include <assets/cook/ProbeBake.h>
#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>
#include <math/Mat.h>
#include <math/spatial/GridTransform3d.h>
#include <assets/static_mesh/MeshGeometry.h>

#include <cstdint>
#include <vector>

struct ProbeVolumeInput
{
    GridTransform3d Grid;
    std::int32_t Priority = 0;
};

struct LightmapPlacement
{
    EntityId Entity;
    std::uint32_t SceneEntityIndex = UINT32_MAX;
    Mat4 ToWorld = Mat4::Identity();
    MeshGeometry Geometry;
    Vec2d WorldExtent;
    std::uint32_t Chart = 0;
    bool CastsIntoBake = true;
};

// The captured document state one cook consumes: geometry, lights, probe
// volumes, halo occluders, resolved lighting parameters, and the passthrough
// scene. Collection gathers only what the resolved graph needs (a collision
// only cook leaves lights, charts, and placements empty); execution never
// infers step selection from a field being empty.
struct DocumentCookSnapshot
{
    JsonValue PassthroughScene;
    LightingCookParams Lighting;
    std::vector<CookBrushGeometry> Brushes;
    CookChartSet Charts;
    std::vector<LightmapPlacement> Placements;
    std::vector<BakeDirectLight> BakeLights;
    std::vector<BakeDirectLight> BounceLights;
    std::vector<ProbeVolumeInput> ProbeVolumes;
    std::vector<ProbeHaloZone> Halo;
};
