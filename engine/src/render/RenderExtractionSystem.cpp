#include <render/RenderExtractionSystem.h>

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

void RenderExtractionSystem::Extract(const TransformStore<Transform3f>& transforms,
                                     const MeshRendererStore& renderers,
                                     const MeshCache& meshes,
                                     const MaterialCache& materials,
                                     const CameraRenderData& camera,
                                     RenderQueue& queue)
{
    const auto items = renderers.GetItems();
    const auto owners = renderers.GetOwnerIds();

    for (size_t i = 0; i < items.size(); ++i)
    {
        const MeshRendererComponent& renderer = items[i];
        if (!renderer.Visible) continue;

        // SparseSet stores raw indices; generation 1 is the minimum valid value.
        EntityId entity{ static_cast<EntityIndex>(owners[i]), 1 };
        const Transform3f* transform = transforms.TryGetWorld(entity);
        const GpuMesh* mesh = meshes.Get(renderer.Mesh);
        const Material* material = materials.Get(renderer.Material);
        if (transform == nullptr || mesh == nullptr || material == nullptr) continue;

        const Mat4 world = transform->ToMat4();
        const Aabb3d worldBounds = TransformBounds(mesh->LocalBounds, world);
        const Vec4 cameraSpaceCenter =
            camera.View * Vec4(worldBounds.Center().X, worldBounds.Center().Y, worldBounds.Center().Z, 1.0f);
        const float cameraDepth = -cameraSpaceCenter.Z;

        for (uint32_t submeshIndex = 0;
             submeshIndex < static_cast<uint32_t>(mesh->Submeshes.size());
             ++submeshIndex)
        {
            if ((renderer.SubmeshMask & (1u << submeshIndex)) == 0) continue;

            RenderQueueItem item{};
            item.Mesh = renderer.Mesh;
            item.Material = renderer.Material;
            item.SubmeshIndex = submeshIndex;
            item.WorldMatrix = world;
            item.WorldBounds = worldBounds;
            item.CameraDepth = cameraDepth;
            item.Pass = material->Pass;
            queue.AddOpaque(item);
        }
    }
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
