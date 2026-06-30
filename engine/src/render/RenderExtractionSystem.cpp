#include <render/RenderExtractionSystem.h>

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
                                     const MaterialSetCache& materialSets,
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
    // ECS migration decision: copied data decouples extraction from
    // submission and tolerates any structural change between extract and draw.
    if (LastWorld != &world || !CachedQuery.has_value())
    {
        CachedQuery.emplace(world);
        LastWorld = &world;
    }

    CachedQuery->ForEachChunk([&](auto& view)
    {
        const auto transforms  = view.template Read<WorldTransform>();
        const auto renderers   = view.template Read<StaticMeshComponent>();
        const uint32_t count   = view.Count();

        for (uint32_t i = 0; i < count; ++i)
        {
            const StaticMeshComponent& renderer = renderers[i];
            if (!renderer.Visible) continue;

            const GpuStaticMesh* mesh = meshes.Get(renderer.Mesh);
            const std::vector<MaterialHandle>* sectionMaterials =
                materialSets.Get(renderer.Materials);
            if (mesh == nullptr || sectionMaterials == nullptr || sectionMaterials->empty())
                continue;

            const Mat4   worldMatrix  = transforms[i].Value.ToMat4();
            const Aabb3d worldBounds  = TransformBounds(mesh->LocalBounds, worldMatrix);

            // Inline frustum cull — skip the item entirely if it's outside
            // the view frustum. This replaces the separate FrustumCullingSystem
            // post-pass from the pre-archetype render path.
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

                // Map the section to its material via MaterialSlot; a slot past
                // the bound set falls back to the last member (so an under-bound
                // set still draws rather than dropping geometry).
                const uint32_t slot = mesh->Sections[sectionIndex].MaterialSlot;
                const MaterialHandle materialHandle = slot < sectionMaterials->size()
                    ? (*sectionMaterials)[slot]
                    : sectionMaterials->back();
                const Material* material = materials.Get(materialHandle);
                if (material == nullptr) continue;

                RenderQueueItem item{};
                item.Mesh         = renderer.Mesh;
                item.Material     = materialHandle;
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
