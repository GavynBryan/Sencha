#include <render/RenderExtractionSystem.h>

#include <ecs/Query.h>
#include <world/transform/TransformComponents.h>

namespace
{
    Aabb3d TransformBounds(const Aabb3d& local, const Mat4& world)
    {
        Aabb3d result = Aabb3d::Empty();
        for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
        for (int z = 0; z < 2; ++z)
        {
            Vec3d p(
                x == 0 ? local.Min.X : local.Max.X,
                y == 0 ? local.Min.Y : local.Max.Y,
                z == 0 ? local.Min.Z : local.Max.Z);
            result.ExpandToInclude(world.TransformPoint(p));
        }
        return result;
    }
}

void RenderExtractionSystem::Extract(const World& world,
                                     const StaticMeshCache& meshes,
                                     const MaterialCache& materials,
                                     const CameraRenderData& camera,
                                     RenderQueue& queue)
{
    if (!world.IsRegistered<WorldTransform>()
        || !world.IsRegistered<StaticMeshComponent>())
    {
        return;
    }

    // Single chunk-level pass: Read<WorldTransform> + Read<StaticMeshComponent>.
    // Frustum culling is inlined — no separate post-pass required.
    // Items are copied into RenderQueueItem (not pointer-into-chunk) per the
    // MigrationPlan Phase 3 decision: copied data decouples extraction from
    // submission and tolerates any structural change between extract and draw.
    Query<Read<WorldTransform>, Read<StaticMeshComponent>> query(const_cast<World&>(world));
    query.ForEachChunk([&](auto& view)
    {
        const auto transforms  = view.template Read<WorldTransform>();
        const auto renderers   = view.template Read<StaticMeshComponent>();
        const uint32_t count   = view.Count();

        for (uint32_t i = 0; i < count; ++i)
        {
            const StaticMeshComponent& renderer = renderers[i];
            if (!renderer.Visible) continue;

            const GpuStaticMesh* mesh     = meshes.Get(renderer.Mesh);
            const Material*      material = materials.Get(renderer.Material);
            if (mesh == nullptr || material == nullptr) continue;

            const Mat4   worldMatrix  = transforms[i].Value.ToMat4();
            const Aabb3d worldBounds  = TransformBounds(mesh->LocalBounds, worldMatrix);

            // Inline frustum cull — skip the item entirely if it's outside
            // the view frustum. This replaces the separate FrustumCullingSystem
            // post-pass (see MigrationPlan.md Phase 3, point 3).
            if (!camera.ViewFrustum.IntersectsAabb(worldBounds))
                continue;

            const Vec4  cameraSpaceCenter =
                camera.View * Vec4(worldBounds.Center().X, worldBounds.Center().Y,
                                   worldBounds.Center().Z, 1.0f);
            const float cameraDepth = -cameraSpaceCenter.Z;

            for (uint32_t sectionIndex = 0;
                 sectionIndex < static_cast<uint32_t>(mesh->Sections.size());
                 ++sectionIndex)
            {
                if ((renderer.SectionMask & (1u << sectionIndex)) == 0) continue;

                RenderQueueItem item{};
                item.Mesh         = renderer.Mesh;
                item.Material     = renderer.Material;
                item.SectionIndex = sectionIndex;
                item.WorldMatrix  = worldMatrix;
                item.WorldBounds  = worldBounds;
                item.CameraDepth  = cameraDepth;
                item.Pass         = material->Pass;
                queue.AddOpaque(item);
            }
        }
    });
}

// Frustum culling is now inlined in Extract above. This no-op exists so
// existing call sites in DefaultRenderPipeline compile without modification.
void FrustumCullingSystem::Cull(const CameraRenderData& /*camera*/, RenderQueue& /*queue*/)
{
}
