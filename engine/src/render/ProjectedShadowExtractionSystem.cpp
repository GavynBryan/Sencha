#include <render/ProjectedShadowExtractionSystem.h>

#include <math/geometry/3d/AabbTransform.h>
#include <world/transform/TransformHistory.h>

bool AppendProjectedShadowCaster(const SkinnedMeshComponent& renderer,
                                 const GpuStaticMesh& mesh,
                                 const Mat4& worldMatrix,
                                 RenderEntityKey key,
                                 ProjectedShadowSet& out)
{
    if (!renderer.Visible || !renderer.CastsProjectedShadow)
        return false;

    out.Casters.push_back(ProjectedShadowCaster{
        .Key = key,
        .Mesh = renderer.Mesh,
        .WorldMatrix = worldMatrix,
        .WorldBounds = TransformAabb(mesh.LocalBounds, worldMatrix),
        .SectionMask = renderer.SectionMask,
    });
    return true;
}

void ProjectedShadowExtractionSystem::Extract(const World& world,
                                              const StoragePartitionSet& partitions,
                                              const SkinnedMeshCache& meshes,
                                              ProjectedShadowSet& out,
                                              double interpolationAlpha)
{
    out.Reset();

    if (!world.IsRegistered<WorldTransform>()
        || !world.IsRegistered<SkinnedMeshComponent>())
    {
        return;
    }

    if (LastWorld != &world || !CachedQuery.has_value())
    {
        CachedQuery.emplace(world);
        CachedInterpolatedQuery.emplace(world);
        LastWorld = &world;
    }

    // The silhouette must use the same pose the mesh renders at, or the
    // shadow separates from the object dropping it.
    const auto emitChunk = [&](auto& view, auto&& poseAt)
    {
        const auto renderers = view.template Read<SkinnedMeshComponent>();

        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            const SkinnedMeshComponent& renderer = renderers[i];
            const GpuStaticMesh* mesh = meshes.Get(renderer.Mesh);
            if (mesh == nullptr)
                continue;

            AppendProjectedShadowCaster(
                renderer, *mesh, poseAt(i).ToMat4(),
                RenderEntityKey{ .Entity = view.Entity(i) }, out);
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
