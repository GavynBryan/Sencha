#include <render/RenderExtractionSystem.h>

#include <world/transform/TransformComponents.h>

#include <algorithm>

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
    world.ForEachComponent<StaticMeshComponent>(
        [&](EntityId entity, const StaticMeshComponent& renderer)
    {
        if (!renderer.Visible) return;

        const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
        const GpuStaticMesh* mesh = meshes.Get(renderer.Mesh);
        const Material* material = materials.Get(renderer.Material);
        if (transform == nullptr || mesh == nullptr || material == nullptr) return;

        const Mat4 worldMatrix = transform->Value.ToMat4();
        const Aabb3d worldBounds = TransformBounds(mesh->LocalBounds, worldMatrix);
        const Vec4 cameraSpaceCenter =
            camera.View * Vec4(worldBounds.Center().X, worldBounds.Center().Y,
                               worldBounds.Center().Z, 1.0f);
        const float cameraDepth = -cameraSpaceCenter.Z;

        for (uint32_t sectionIndex = 0;
             sectionIndex < static_cast<uint32_t>(mesh->Sections.size());
             ++sectionIndex)
        {
            if ((renderer.SectionMask & (1u << sectionIndex)) == 0) continue;

            RenderQueueItem item{};
            item.Mesh = renderer.Mesh;
            item.Material = renderer.Material;
            item.SectionIndex = sectionIndex;
            item.WorldMatrix = worldMatrix;
            item.WorldBounds = worldBounds;
            item.CameraDepth = cameraDepth;
            item.Pass = material->Pass;
            queue.AddOpaque(item);
        }
    });
}

void FrustumCullingSystem::Cull(const CameraRenderData& camera, RenderQueue& queue)
{
    auto& opaque = queue.Opaque();
    opaque.erase(std::remove_if(opaque.begin(), opaque.end(),
                                [&camera](const RenderQueueItem& item) {
                                    return !camera.ViewFrustum.IntersectsAabb(item.WorldBounds);
                                }),
                 opaque.end());
}
