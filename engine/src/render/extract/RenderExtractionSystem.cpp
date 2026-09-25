#include <render/extract/RenderExtractionSystem.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>

#include <anim/AnimPosePool.h>
#include <anim/AnimPoseState.h>
#include <anim/AnimationClipSampling.h>
#include <anim/SkinningPalette.h>

#include <world/transform/TransformHistory.h>

#include <assets/texture/TextureCache.h>
#include <render/MeshDrawInstance.h>
#include <render/ZoneLightmapComponent.h>

#include <algorithm>

namespace
{
std::size_t TableSlot(StoragePartitionId partition)
{
    return static_cast<std::size_t>(partition.Value);
}
} // namespace

void CollectZoneLightmaps(
    const World& world,
    const StoragePartitionSet& partitions,
    std::vector<ZoneLightmapBinding>& out)
{
    out.clear();
    if (!world.IsRegistered<ZoneLightmapComponent>())
        return;

    world.ForEachComponent<ZoneLightmapComponent>(
        [&](EntityId entity, const ZoneLightmapComponent& lightmap)
        {
            const StoragePartitionId partition = world.GetEntityPartition(entity);
            if (!partitions.Contains(partition))
                return;
            out.push_back(ZoneLightmapBinding{
                .Partition = partition,
                .Texture = lightmap.Texture,
                .Ao = lightmap.Ao,
            });
        });
}

void ResolveZoneLightmapIndices(
    std::span<const ZoneLightmapBinding> bindings,
    const TextureCache& textures,
    std::vector<std::pair<StoragePartitionId, ZoneLightmapIndices>>& out)
{
    out.clear();
    out.reserve(bindings.size());
    for (const ZoneLightmapBinding& binding : bindings)
    {
        ZoneLightmapIndices indices;
        const BindlessImageIndex lightmap =
            textures.GetBindlessIndex(binding.Texture);
        if (lightmap.IsValid())
            indices.Lightmap = lightmap.Value;
        const BindlessImageIndex ao = textures.GetBindlessIndex(binding.Ao);
        if (ao.IsValid())
            indices.Ao = ao.Value;
        out.emplace_back(binding.Partition, indices);
    }
}

void BuildZoneLightmapTable(
    std::span<const std::pair<StoragePartitionId, ZoneLightmapIndices>> resolved,
    std::vector<ZoneLightmapIndices>& table)
{
    table.clear();
    if (resolved.empty())
        return;

    std::size_t highest = 0;
    for (const auto& [partition, indices] : resolved)
        highest = std::max(highest, TableSlot(partition));

    table.resize(highest + 1, ZoneLightmapIndices{});
    for (const auto& [partition, indices] : resolved)
        table[TableSlot(partition)] = indices;
}

ZoneLightmapIndices LookupZoneLightmap(
    std::span<const ZoneLightmapIndices> table,
    StoragePartitionId partition)
{
    const std::size_t slot = TableSlot(partition);
    return slot < table.size() ? table[slot] : ZoneLightmapIndices{};
}

void RenderExtractionSystem::Extract(
    const World& world,
    const StoragePartitionSet& partitions,
    const StaticMeshCache& meshes,
    const MaterialCache& materials,
    const MaterialSetCache& materialSets,
    const CameraRenderData& camera,
    RenderQueue& queue,
    const TextureCache* textures,
    double interpolationAlpha)
{
    Extract(world, partitions,
            RenderExtractCaches{ meshes, materials, materialSets, textures },
            camera, queue, interpolationAlpha);
}

void RenderExtractionSystem::Extract(
    const World& world,
    const StoragePartitionSet& partitions,
    const RenderExtractCaches& caches,
    const CameraRenderData& camera,
    RenderQueue& queue,
    double interpolationAlpha,
    SkinnedPoseFrameData* skinnedPoses)
{
    if (!world.IsRegistered<WorldTransform>()
        || !world.IsRegistered<StaticMeshComponent>())
    {
        return;
    }

    ResolveLightmaps(world, partitions, caches.Textures);

    const bool skinnedRegistered = world.IsRegistered<SkinnedMeshComponent>();
    EnsureQueries(world, skinnedRegistered);

    EmitStaticMeshes(partitions, caches, camera, queue, interpolationAlpha);

    if (caches.SkinnedMeshes == nullptr || !skinnedRegistered)
        return;
    EmitSkinnedMeshes(world, partitions, caches, camera, queue,
                      interpolationAlpha, skinnedPoses);
}

void RenderExtractionSystem::ResolveLightmaps(
    const World& world, const StoragePartitionSet& partitions,
    const TextureCache* textures)
{
    // Each resident zone owns its own baked atlas, so the indices are resolved
    // per partition and looked up per chunk. Resolving one atlas for the whole
    // world would stamp whichever zone happened to be visited last onto every
    // other zone's meshes.
    LightmapTable.clear();
    if (textures == nullptr)
        return;
    CollectZoneLightmaps(world, partitions, LightmapBindings);
    ResolveZoneLightmapIndices(LightmapBindings, *textures, ResolvedLightmaps);
    BuildZoneLightmapTable(ResolvedLightmaps, LightmapTable);
}

void RenderExtractionSystem::EnsureQueries(const World& world,
                                           bool skinnedRegistered)
{
    if (LastWorld == &world && CachedQuery.has_value())
        return;
    CachedQuery.emplace(world);
    CachedInterpolatedQuery.emplace(world);
    CachedSkinnedQuery.reset();
    CachedSkinnedInterpolatedQuery.reset();
    if (skinnedRegistered)
    {
        CachedSkinnedQuery.emplace(world);
        CachedSkinnedInterpolatedQuery.emplace(world);
    }
    LastWorld = &world;
}

void RenderExtractionSystem::EmitStaticMeshes(
    const StoragePartitionSet& partitions, const RenderExtractCaches& caches,
    const CameraRenderData& camera, RenderQueue& queue,
    double interpolationAlpha)
{
    // Whether an entity carries pose history is an archetype property, so the
    // two paths are separate chunk walks rather than a per-entity branch.
    const auto emitChunk = [&](auto& view, auto&& poseAt)
    {
        const auto renderers = view.template Read<StaticMeshComponent>();
        const ZoneLightmapIndices lightmap =
            LookupZoneLightmap(LightmapTable, view.Partition());

        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            const StaticMeshComponent& renderer = renderers[i];
            if (!renderer.Visible)
                continue;

            if (view.Entity(i) == camera.ExcludedEntity)
                continue;

            const GpuStaticMesh* mesh = caches.Meshes.Get(renderer.Mesh);
            const std::vector<MaterialHandle>* sectionMaterials =
                caches.MaterialSets.Get(renderer.Materials);
            if (mesh == nullptr || sectionMaterials == nullptr
                || sectionMaterials->empty())
            {
                continue;
            }

            const Mat4 worldMatrix = poseAt(i).ToMat4();
            const Aabb3d worldBounds =
                TransformAabb(mesh->LocalBounds, worldMatrix);
            if (!camera.ViewFrustum.IntersectsAabb(worldBounds))
                continue;

            const Vec4 cameraSpaceCenter = camera.View * Vec4(
                worldBounds.Center().X,
                worldBounds.Center().Y,
                worldBounds.Center().Z,
                1.0f);

            MeshDrawInstance instance;
            instance.Mesh = renderer.Mesh;
            instance.WorldMatrix = worldMatrix;
            instance.WorldBounds = worldBounds;
            instance.SectionMask = renderer.SectionMask;
            instance.CameraDepth = -cameraSpaceCenter.Z;
            instance.LightmapTextureIndex = lightmap.Lightmap;
            instance.AoTextureIndex = lightmap.Ao;
            instance.LightmapScaleBias = renderer.LightmapScaleBias;
            EmitMeshSections(instance, *mesh, *sectionMaterials,
                             caches.Materials, queue);
        }
    };

    CachedQuery->ForEachChunkIn(partitions, [&](auto& view)
    {
        const auto transforms = view.template Read<WorldTransform>();
        emitChunk(view, [&](uint32_t i) -> const Transform3f& { return transforms[i].Value; });
    });

    CachedInterpolatedQuery->ForEachChunkIn(partitions, [&](auto& view)
    {
        const auto histories = view.template Read<WorldTransformHistory>();
        emitChunk(view, [&](uint32_t i) {
            return ResolvePresentationPose(histories[i], interpolationAlpha);
        });
    });
}

// Skinned instances, drawn at rest until a pose source exists: the entry's
// rest geometry is a GpuStaticMesh like any other, so the expansion is the
// same -- only the residency it resolves through differs. No lightmap and
// no atlas stamp: a skinned mesh is the canonical movable non-receiver.
void RenderExtractionSystem::EmitSkinnedMeshes(
    const World& world, const StoragePartitionSet& partitions,
    const RenderExtractCaches& caches, const CameraRenderData& camera,
    RenderQueue& queue, double interpolationAlpha,
    SkinnedPoseFrameData* skinnedPoses)
{
    const SkinnedMeshCache& skinnedMeshes = *caches.SkinnedMeshes;

    const auto emitSkinnedChunk = [&](auto& view, auto&& poseAt, double poseAlpha)
    {
        const auto renderers = view.template Read<SkinnedMeshComponent>();

        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            const SkinnedMeshComponent& renderer = renderers[i];
            if (!renderer.Visible)
                continue;

            if (view.Entity(i) == camera.ExcludedEntity)
                continue;

            const GpuStaticMesh* mesh = skinnedMeshes.Get(renderer.Mesh);
            const std::vector<MaterialHandle>* sectionMaterials =
                caches.MaterialSets.Get(renderer.Materials);
            if (mesh == nullptr || sectionMaterials == nullptr
                || sectionMaterials->empty())
            {
                continue;
            }

            const Mat4 worldMatrix = poseAt(i).ToMat4();
            const Aabb3d worldBounds =
                TransformAabb(mesh->LocalBounds, worldMatrix);
            if (!camera.ViewFrustum.IntersectsAabb(worldBounds))
                continue;

            const Vec4 cameraSpaceCenter = camera.View * Vec4(
                worldBounds.Center().X,
                worldBounds.Center().Y,
                worldBounds.Center().Z,
                1.0f);

            std::uint32_t poseSlot = UINT32_MAX;
            if (skinnedPoses != nullptr)
            {
                poseSlot = RegisterSkinnedPose(world, caches, renderer,
                                               view.Entity(i), poseAlpha, *skinnedPoses);
            }

            MeshDrawInstance instance;
            instance.SkinnedMesh = renderer.Mesh;
            instance.WorldMatrix = worldMatrix;
            instance.WorldBounds = worldBounds;
            instance.SectionMask = renderer.SectionMask;
            instance.CameraDepth = -cameraSpaceCenter.Z;
            instance.PoseSlot = poseSlot;
            EmitMeshSections(instance, *mesh, *sectionMaterials,
                             caches.Materials, queue);
        }
    };

    CachedSkinnedQuery->ForEachChunkIn(partitions, [&](auto& view)
    {
        const auto transforms = view.template Read<WorldTransform>();
        // Drawn where the last tick left it, so posed as the last tick left it.
        emitSkinnedChunk(view, [&](uint32_t i) -> const Transform3f& { return transforms[i].Value; }, 1.0);
    });

    CachedSkinnedInterpolatedQuery->ForEachChunkIn(partitions, [&](auto& view)
    {
        const auto histories = view.template Read<WorldTransformHistory>();
        emitSkinnedChunk(view, [&](uint32_t i) {
            return ResolvePresentationPose(histories[i], interpolationAlpha);
        }, interpolationAlpha);
    });
}

std::uint32_t RenderExtractionSystem::RegisterSkinnedPose(
    const World& world, const RenderExtractCaches& caches,
    const SkinnedMeshComponent& renderer, EntityId entity, double poseAlpha,
    SkinnedPoseFrameData& skinnedPoses)
{
    const SkinnedMeshCache& skinnedMeshes = *caches.SkinnedMeshes;

    // One slot per entity (every section shares it). A clip player poses the
    // skeleton at its current time; without one the palette stays the bind
    // identity, which reproduces the rest bytes exactly.
    const MeshSkinning* skinning = skinnedMeshes.GetSkinning(renderer.Mesh);
    if (skinning == nullptr || skinning->JointCount == 0)
        return UINT32_MAX;

    const std::uint32_t poseSlot = skinnedPoses.AppendInstance(
        renderer.Mesh, RenderEntityKey{ .Entity = entity }, skinning->JointCount);
    const std::uint32_t paletteOffset =
        skinnedPoses.Instances[poseSlot].PaletteOffset;

    const SkeletonHandle skeletonHandle = skinnedMeshes.GetSkeletonHandle(renderer.Mesh);
    const SkeletonData* skeleton =
        caches.Skeletons != nullptr ? caches.Skeletons->Get(skeletonHandle) : nullptr;
    if (skeleton == nullptr || skeleton->Joints.size() != skinning->JointCount)
        return poseSlot;

    // A rig's pose was made on the fixed tick by the pose pass; this copies
    // it, between the last two ticks by the same share the entity's transform
    // is drawn at, and never evaluates animation.
    const AnimPosePool* pool = world.TryGetResource<AnimPosePool>();
    const AnimPoseState* posed =
        pool != nullptr && world.IsRegistered<AnimPoseState>() ? world.TryGet<AnimPoseState>(entity) : nullptr;
    if (const AnimPosePool::Slot* slot = posed != nullptr ? pool->Find(posed->Slot) : nullptr;
        slot != nullptr && slot->HasCurrent && slot->Skeleton == skeletonHandle
        && slot->Current.size() == skeleton->Joints.size())
    {
        PoseScratch.resize(slot->Current.size());
        const float alpha = static_cast<float>(std::clamp(poseAlpha, 0.0, 1.0));
        for (std::size_t j = 0; j < slot->Current.size(); ++j)
            PoseScratch[j] = slot->HasPrevious && alpha < 1.0f
                ? Transform3f::Interpolate(slot->Previous[j], slot->Current[j], alpha)
                : slot->Current[j];
        BuildPosedModelTransforms(*skeleton, PoseScratch, ModelScratch);
        BuildSkinningPalette(*skeleton, ModelScratch, PaletteScratch);
        std::copy(PaletteScratch.begin(), PaletteScratch.end(), skinnedPoses.Palettes.begin() + paletteOffset);
    }
    return poseSlot;
}
