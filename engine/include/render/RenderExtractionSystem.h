#pragma once

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/RenderQueue.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <world/transform/TransformComponents.h>

#include <optional>

//=============================================================================
// RenderExtractionSystem
//
// Walks visible-partition StaticMeshComponents and emits one RenderQueueItem
// per enabled section into the RenderQueue. World-space bounds are computed
// here for use by the subsequent culling pass.
//
// The query is cached per instance to avoid rebuild-from-scratch every frame;
// a World* sentinel detects world changes.
//=============================================================================
class TextureCache;

class RenderExtractionSystem
{
public:
    // `textures` resolves the zone's ZoneLightmapComponent (if any) to the
    // bindless atlas index stamped on every emitted item; null leaves items
    // without a lightmap.
    void Extract(
        const World& world,
        const StoragePartitionSet& partitions,
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
