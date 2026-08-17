#pragma once

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/RenderQueue.h>
#include <render/StaticMeshComponent.h>
#include <render/TextureHandle.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

class TextureCache;

// One resident zone's baked-lighting atlas and the AO plane sharing its layout,
// keyed by the storage partition whose meshes it applies to. A zone that baked
// no lighting contributes no binding.
struct ZoneLightmapBinding
{
    StoragePartitionId Partition;
    TextureHandle Texture;
    TextureHandle Ao;
};

// Bindless indices for one partition, in the form draw items carry them.
struct ZoneLightmapIndices
{
    std::uint32_t Lightmap = UINT32_MAX;
    std::uint32_t Ao = UINT32_MAX;
};

// Gathers the lightmap binding of every partition in `partitions` that authored
// one. Several zones can be resident at once and each owns its own atlas, so
// the result is per partition, never per world. Pure: resolves no GPU handle
// and touches no cache, so it is testable without a device.
void CollectZoneLightmaps(
    const World& world,
    const StoragePartitionSet& partitions,
    std::vector<ZoneLightmapBinding>& out);

// Turns each binding's texture handles into the bindless slots draw items
// carry. A handle the cache cannot resolve leaves that slot invalid, which the
// shader reads as "no baked contribution" rather than as an error.
void ResolveZoneLightmapIndices(
    std::span<const ZoneLightmapBinding> bindings,
    const TextureCache& textures,
    std::vector<std::pair<StoragePartitionId, ZoneLightmapIndices>>& out);

// Scatters already-resolved per-partition indices into a table indexed by
// partition value, so the extraction loop answers "which atlas does this chunk
// sample?" with one bounds check and one index. Entries for partitions without
// a lightmap keep the invalid default. Pure.
void BuildZoneLightmapTable(
    std::span<const std::pair<StoragePartitionId, ZoneLightmapIndices>> resolved,
    std::vector<ZoneLightmapIndices>& table);

// Reads a partition's entry out of a table built above. Partitions past the
// table's end have no lightmap.
[[nodiscard]] ZoneLightmapIndices LookupZoneLightmap(
    std::span<const ZoneLightmapIndices> table,
    StoragePartitionId partition);

// The read-only caches one extraction resolves against. `Textures` stays
// optional: without it, items carry no lightmap. Holds references into the
// caller's frame and owns nothing, so constructing one allocates nothing.
struct RenderExtractCaches
{
    const StaticMeshCache& Meshes;
    const MaterialCache& Materials;
    const MaterialSetCache& MaterialSets;
    const TextureCache* Textures = nullptr;
};

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

class RenderExtractionSystem
{
public:
    // `interpolationAlpha` is how far this frame sits past the last completed
    // simulation tick (PresentationTime::Alpha). Entities carrying
    // WorldTransformHistory render the blend at that point; everything else
    // renders its live WorldTransform.
    void Extract(
        const World& world,
        const StoragePartitionSet& partitions,
        const RenderExtractCaches& caches,
        const CameraRenderData& camera,
        RenderQueue& queue,
        double interpolationAlpha = 1.0);

    // Same extraction with the caches spelled out. Retained for callers built
    // against the older signature; forwards to the bundled form.
    void Extract(
        const World& world,
        const StoragePartitionSet& partitions,
        const StaticMeshCache& meshes,
        const MaterialCache& materials,
        const MaterialSetCache& materialSets,
        const CameraRenderData& camera,
        RenderQueue& queue,
        const TextureCache* textures = nullptr,
        double interpolationAlpha = 1.0);

private:
    const World* LastWorld = nullptr;
    std::optional<Query<Read<WorldTransform>,
                        Read<StaticMeshComponent>,
                        Without<WorldTransformHistory>>> CachedQuery;
    std::optional<Query<Read<WorldTransformHistory>,
                        Read<StaticMeshComponent>>> CachedInterpolatedQuery;
    // Retained across frames so a steady-state extract allocates nothing; both
    // are rebuilt from scratch each call.
    std::vector<ZoneLightmapBinding> LightmapBindings;
    std::vector<std::pair<StoragePartitionId, ZoneLightmapIndices>> ResolvedLightmaps;
    std::vector<ZoneLightmapIndices> LightmapTable;
};
