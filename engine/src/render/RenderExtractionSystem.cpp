#include <render/RenderExtractionSystem.h>

#include <graphics/vulkan/TextureCache.h>
#include <render/ZoneLightmapComponent.h>

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
                                     RenderQueue& queue,
                                     const TextureCache* textures)
{
    if (!world.IsRegistered<WorldTransform>()
        || !world.IsRegistered<StaticMeshComponent>())
    {
        return;
    }

    // The zone's baked-lighting atlas and AO plane, resolved once per
    // registry pass.
    uint32_t lightmapIndex = UINT32_MAX;
    uint32_t aoIndex = UINT32_MAX;
    if (textures != nullptr && world.IsRegistered<ZoneLightmapComponent>())
    {
        world.ForEachComponent<ZoneLightmapComponent>(
            [&](EntityId, const ZoneLightmapComponent& lightmap)
            {
                const BindlessImageIndex index =
                    textures->GetBindlessIndex(lightmap.Texture);
                if (index.IsValid())
                    lightmapIndex = index.Value;
                const BindlessImageIndex ao =
                    textures->GetBindlessIndex(lightmap.Ao);
                if (ao.IsValid())
                    aoIndex = ao.Value;
            });
    }

    if (LastWorld != &world || !CachedQuery.has_value())
    {
        CachedQuery.emplace(world);
        LastWorld = &world;
    }

    CachedQuery->ForEachChunk([&](auto& view)
    {
        const auto transforms = view.template Read<WorldTransform>();
        const auto renderers = view.template Read<StaticMeshComponent>();
        const uint32_t count = view.Count();

        for (uint32_t i = 0; i < count; ++i)
        {
            const StaticMeshComponent& renderer = renderers[i];
            if (!renderer.Visible)
                continue;

            const GpuStaticMesh* mesh = meshes.Get(renderer.Mesh);
            const std::vector<MaterialHandle>* sectionMaterials =
                materialSets.Get(renderer.Materials);
            if (mesh == nullptr || sectionMaterials == nullptr || sectionMaterials->empty())
                continue;

            const Mat4 worldMatrix = transforms[i].Value.ToMat4();
            const Aabb3d worldBounds = TransformBounds(mesh->LocalBounds, worldMatrix);
            if (!camera.ViewFrustum.IntersectsAabb(worldBounds))
                continue;

            const Vec4 cameraSpaceCenter =
                camera.View * Vec4(worldBounds.Center().X, worldBounds.Center().Y,
                                   worldBounds.Center().Z, 1.0f);
            const float cameraDepth = -cameraSpaceCenter.Z;

            for (uint32_t sectionIndex = 0;
                 sectionIndex < static_cast<uint32_t>(mesh->Sections.size());
                 ++sectionIndex)
            {
                if ((renderer.SectionMask & (1u << sectionIndex)) == 0)
                    continue;

                const uint32_t slot = mesh->Sections[sectionIndex].MaterialSlot;
                const MaterialHandle materialHandle = slot < sectionMaterials->size()
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
                item.LightmapTextureIndex = lightmapIndex;
                item.AoTextureIndex = aoIndex;
                item.LightmapScaleBias = renderer.LightmapScaleBias;
                queue.AddOpaque(item);
            }
        }
    });
}
