#pragma once

#include "BrushCookInput.h"
#include "CookGraph.h"
#include "DocumentCook.h"

#include <assets/cook/DirectLightBake.h>
#include <ecs/EntityId.h>
#include <math/Mat.h>
#include <math/spatial/GridTransform3d.h>
#include <render/static_mesh/MeshGeometry.h>

#include <cstdint>
#include <memory>
#include <string>
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

struct DocumentCookInput::Data
{
    CookProfile Profile;
    ResolvedCookGraph Graph;
    bool ForceRebuild = false;
    std::string OutputNamespace;
    std::shared_ptr<CookControl> Control;
    bool RunDirectLightmap = false;
    bool RunAmbientOcclusion = false;
    bool RunProbeBake = false;
    bool RunCollisionTarget = false;
    bool RunReferencedAssets = false;
    double CellSize = 16.0;
    LightingCookParams Lighting;
    JsonValue PassthroughScene;
    std::vector<BakeDirectLight> BakeLights;
    std::vector<BakeDirectLight> BounceLights;
    std::vector<ProbeVolumeInput> ProbeVolumes;
    CookChartSet Charts;
    std::vector<CookBrushGeometry> Brushes;
    std::vector<LightmapPlacement> Placements;
    std::vector<ProbeHaloZone> Halo;
};
