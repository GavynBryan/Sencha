#include <render/RenderExtractionSystem.h>

#include <world/transform/TransformHistory.h>

#include <graphics/vulkan/TextureCache.h>
#include <render/ZoneLightmapComponent.h>

#include <algorithm>

namespace
{
std::size_t TableSlot(StoragePartitionId partition)
{
    return static_cast<std::size_t>(partition.Value);
}

Aabb3d TransformBounds(const Aabb3d& local, const Mat4& world)
{
    Aabb3d result = Aabb3d::Empty();
    for (int x = 0; x < 2; ++x)
    for (int y = 0; y < 2; ++y)
    for (int z = 0; z < 2; ++z)
    {
        Vec3d point(
            x == 0 ? local.Min.X : local.Max.X,
            y == 0 ? local.Min.Y : local.Max.Y,
            z == 0 ? local.Min.Z : local.Max.Z);
        result.ExpandToInclude(world.TransformPoint(point));
    }
    return result;
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
        ResolvedLightmaps.clear();
        ResolvedLightmaps.reserve(LightmapBindings.size());
        for (const ZoneLightmapBinding& binding : LightmapBindings)
        {
            ZoneLightmapIndices indices;
            const BindlessImageIndex lightmap =
                textures->GetBindlessIndex(binding.Texture);
            if (lightmap.IsValid())
                indices.Lightmap = lightmap.Value;
            const BindlessImageIndex ao =
                textures->GetBindlessIndex(binding.Ao);
            if (ao.IsValid())
                indices.Ao = ao.Value;
            ResolvedLightmaps.emplace_back(binding.Partition, indices);
        }
        BuildZoneLightmapTable(ResolvedLightmaps, LightmapTable);
    }

    if (LastWorld != &world || !CachedQuery.has_value())
    {
        CachedQuery.emplace(world);
        CachedInterpolatedQuery.emplace(world);
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
                TransformBounds(mesh->LocalBounds, worldMatrix);
            if (!camera.ViewFrustum.IntersectsAabb(worldBounds))
                continue;

            const Vec4 cameraSpaceCenter = camera.View * Vec4(
                worldBounds.Center().X,
                worldBounds.Center().Y,
                worldBounds.Center().Z,
                1.0f);
            const float cameraDepth = -cameraSpaceCenter.Z;

            for (uint32_t sectionIndex = 0;
                 sectionIndex < static_cast<uint32_t>(mesh->Sections.size());
                 ++sectionIndex)
            {
                if ((renderer.SectionMask & (1u << sectionIndex)) == 0)
                    continue;

                const uint32_t slot =
                    mesh->Sections[sectionIndex].MaterialSlot;
                const MaterialHandle materialHandle =
                    slot < sectionMaterials->size()
                        ? (*sectionMaterials)[slot]
                        : sectionMaterials->back();
                const Material* material = materials.Get(materialHandle);
                if (material == nullptr)
                    continue;

                RenderQueueItem item{};
                item.Mesh = renderer.Mesh;
                item.Material = materialHandle;
                item.SectionIndex = sectionIndex;
                item.WorldMatrix = worldMatrix;
                item.WorldBounds = worldBounds;
                item.CameraDepth = cameraDepth;
                item.Pass = material->Pass;
                item.Pipeline = SelectOpaquePipeline(*material);
                item.LightmapTextureIndex = lightmap.Lightmap;
                item.AoTextureIndex = lightmap.Ao;
                item.LightmapScaleBias = renderer.LightmapScaleBias;
                queue.AddOpaque(item);
            }
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
