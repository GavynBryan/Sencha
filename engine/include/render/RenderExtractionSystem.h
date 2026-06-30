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
// a World* sentinel detects world changes (relevant in multi-registry loops).
//=============================================================================
class RenderExtractionSystem
{
public:
    void Extract(const World& world,
                 const StaticMeshCache& meshes,
                 const MaterialCache& materials,
                 const MaterialSetCache& materialSets,
                 const CameraRenderData& camera,
                 RenderQueue& queue);

private:
    const World* LastWorld = nullptr;
    std::optional<Query<Read<WorldTransform>, Read<StaticMeshComponent>>> CachedQuery;
};
