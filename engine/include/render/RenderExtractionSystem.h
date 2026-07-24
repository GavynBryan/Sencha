#pragma once

#include <ecs/Query.h>
#include <ecs/World.h>
#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <render/RenderQueue.h>
#include <world/transform/TransformComponents.h>

#include <optional>

//=============================================================================
// RenderExtractionSystem
//
// Walks all visible StaticMeshComponents and emits one RenderQueueItem per
// enabled section into the RenderQueue. World-space bounds are computed here
// for use by the subsequent culling pass.
//
// The query is cached per instance to avoid rebuild-from-scratch every frame;
// a World* sentinel detects world changes. One slot, so a loop over several
// active registries rebuilds on each. Measured at 0.038 ms per frame for the
// whole mesh walk, which is not worth keying a cache on world addresses that
// a streamed-out zone can free and a new one reuse.
//=============================================================================
class TextureCache;

class RenderExtractionSystem
{
public:
    // `textures` resolves the zone's ZoneLightmapComponent (if any) to the
    // bindless atlas index stamped on every emitted item; null leaves items
    // without a lightmap.
    void Extract(const World& world,
                 const StaticMeshCache& meshes,
                 const MaterialCache& materials,
                 const MaterialSetCache& materialSets,
                 const CameraRenderData& camera,
                 RenderQueue& queue,
                 const TextureCache* textures = nullptr);

private:
    const World* LastWorld = nullptr;
    std::optional<Query<Read<WorldTransform>, Read<StaticMeshComponent>>> CachedQuery;
};
