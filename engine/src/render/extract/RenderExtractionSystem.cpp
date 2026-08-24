#include <render/extract/RenderExtractionSystem.h>

#include <anim/AnimationClipPlayerComponent.h>
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
    const StaticMeshCache& meshes = caches.Meshes;
    const MaterialCache& materials = caches.Materials;
    const MaterialSetCache& materialSets = caches.MaterialSets;
    const TextureCache* textures = caches.Textures;

    if (!world.IsRegistered<WorldTransform>()
        || !world.IsRegistered<StaticMeshComponent>())
    {
        return;
    }

    // Each resident zone owns its own baked atlas, so the indices are resolved
    // per partition and looked up per chunk. Resolving one atlas for the whole
    // world would stamp whichever zone happened to be visited last onto every
    // other zone's meshes.
    LightmapTable.clear();
    if (textures != nullptr)
    {
        CollectZoneLightmaps(world, partitions, LightmapBindings);
        ResolveZoneLightmapIndices(LightmapBindings, *textures, ResolvedLightmaps);
        BuildZoneLightmapTable(ResolvedLightmaps, LightmapTable);
    }

    const bool skinnedRegistered = world.IsRegistered<SkinnedMeshComponent>();
    if (LastWorld != &world || !CachedQuery.has_value())
    {
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

            const GpuStaticMesh* mesh = meshes.Get(renderer.Mesh);
            const std::vector<MaterialHandle>* sectionMaterials =
                materialSets.Get(renderer.Materials);
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
            EmitMeshSections(instance, *mesh, *sectionMaterials, materials, queue);
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

    // Skinned instances, drawn at rest until a pose source exists: the entry's
    // rest geometry is a GpuStaticMesh like any other, so the expansion is the
    // same -- only the residency it resolves through differs. No lightmap and
    // no atlas stamp: a skinned mesh is the canonical movable non-receiver.
    const SkinnedMeshCache* skinnedMeshes = caches.SkinnedMeshes;
    if (skinnedMeshes == nullptr || !skinnedRegistered)
        return;

    const auto emitSkinnedChunk = [&](auto& view, auto&& poseAt)
    {
        const auto renderers = view.template Read<SkinnedMeshComponent>();

        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            const SkinnedMeshComponent& renderer = renderers[i];
            if (!renderer.Visible)
                continue;

            if (view.Entity(i) == camera.ExcludedEntity)
                continue;

            const GpuStaticMesh* mesh = skinnedMeshes->Get(renderer.Mesh);
            const std::vector<MaterialHandle>* sectionMaterials =
                materialSets.Get(renderer.Materials);
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

            // Register the entity's pose slot: one per entity (every section
            // shares it). A clip player poses the skeleton at its current
            // time; without one the palette stays the bind identity, which
            // reproduces the rest bytes exactly.
            std::uint32_t poseSlot = UINT32_MAX;
            if (skinnedPoses != nullptr)
            {
                const MeshSkinning* skinning =
                    skinnedMeshes->GetSkinning(renderer.Mesh);
                if (skinning != nullptr && skinning->JointCount > 0)
                {
                    poseSlot = static_cast<std::uint32_t>(
                        skinnedPoses->Instances.size());
                    const auto paletteOffset = static_cast<std::uint32_t>(
                        skinnedPoses->Palettes.size());
                    skinnedPoses->Instances.push_back(SkinnedPoseInstance{
                        .Mesh = renderer.Mesh,
                        .Key = RenderEntityKey{ .Entity = view.Entity(i) },
                        .PaletteOffset = paletteOffset,
                        .JointCount = skinning->JointCount,
                    });
                    skinnedPoses->Palettes.resize(
                        paletteOffset + skinning->JointCount, Mat4::Identity());

                    // Pose evaluation is per rendered frame, not per tick:
                    // the player advanced its time on the fixed tick and
                    // this samples whatever it currently holds.
                    const SkeletonData* skeleton =
                        caches.Skeletons != nullptr
                            ? caches.Skeletons->Get(
                                  skinnedMeshes->GetSkeletonHandle(renderer.Mesh))
                            : nullptr;
                    const AnimationClipPlayerComponent* player =
                        world.TryGet<AnimationClipPlayerComponent>(view.Entity(i));
                    const AnimationClipData* clip =
                        (player != nullptr && caches.AnimationClips != nullptr)
                            ? caches.AnimationClips->Get(player->Clip)
                            : nullptr;
                    if (clip != nullptr && skeleton != nullptr
                        && skeleton->Joints.size() == skinning->JointCount)
                    {
                        SampleAnimationClip(*clip, *skeleton,
                                            player->TimeSeconds, PoseScratch);
                        BuildPosedModelTransforms(*skeleton, PoseScratch,
                                                  ModelScratch);
                        BuildSkinningPalette(*skeleton, ModelScratch,
                                             PaletteScratch);
                        std::copy(PaletteScratch.begin(), PaletteScratch.end(),
                                  skinnedPoses->Palettes.begin() + paletteOffset);
                    }
                }
            }

            MeshDrawInstance instance;
            instance.SkinnedMesh = renderer.Mesh;
            instance.WorldMatrix = worldMatrix;
            instance.WorldBounds = worldBounds;
            instance.SectionMask = renderer.SectionMask;
            instance.CameraDepth = -cameraSpaceCenter.Z;
            instance.PoseSlot = poseSlot;
            EmitMeshSections(instance, *mesh, *sectionMaterials, materials, queue);
        }
    };

    CachedSkinnedQuery->ForEachChunkIn(partitions, [&](auto& view)
    {
        const auto transforms = view.template Read<WorldTransform>();
        emitSkinnedChunk(view, [&](uint32_t i) -> const Transform3f& { return transforms[i].Value; });
    });

    CachedSkinnedInterpolatedQuery->ForEachChunkIn(partitions, [&](auto& view)
    {
        const auto histories = view.template Read<WorldTransformHistory>();
        emitSkinnedChunk(view, [&](uint32_t i) {
            return ResolvePresentationPose(histories[i], interpolationAlpha);
        });
    });
}
