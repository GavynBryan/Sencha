#include <render/ShadowCasterExtractionSystem.h>

#include <render/StaticMeshComponent.h>
#include <world/registry/Registry.h>
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
            const Vec3d point(
                x == 0 ? local.Min.X : local.Max.X,
                y == 0 ? local.Min.Y : local.Max.Y,
                z == 0 ? local.Min.Z : local.Max.Z);
            result.ExpandToInclude(world.TransformPoint(point));
        }
        return result;
    }
}

void ShadowCasterExtractionSystem::Extract(
    std::span<Registry*> registries,
    const StaticMeshCache& meshes,
    const MaterialCache& materials,
    const MaterialSetCache& materialSets,
    ShadowCasterSet& casters) const
{
    casters.Reset();

    for (Registry* registry : registries)
    {
        if (registry == nullptr)
            continue;

        const World& world = registry->Components;
        if (!world.IsRegistered<WorldTransform>()
            || !world.IsRegistered<StaticMeshComponent>())
        {
            continue;
        }

        world.ForEachComponent<StaticMeshComponent>(
            [&](EntityId entity, const StaticMeshComponent& renderer)
            {
                if (!renderer.Visible || !renderer.CastShadows)
                    return;

                const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
                const GpuStaticMesh* mesh = meshes.Get(renderer.Mesh);
                const std::vector<MaterialHandle>* sectionMaterials =
                    materialSets.Get(renderer.Materials);
                if (transform == nullptr || mesh == nullptr
                    || sectionMaterials == nullptr || sectionMaterials->empty())
                {
                    return;
                }

                const Mat4 worldMatrix = transform->Value.ToMat4();
                const Aabb3d worldBounds = TransformBounds(mesh->LocalBounds, worldMatrix);
                for (std::uint32_t sectionIndex = 0;
                     sectionIndex < static_cast<std::uint32_t>(mesh->Sections.size());
                     ++sectionIndex)
                {
                    if ((renderer.SectionMask & (1u << sectionIndex)) == 0)
                        continue;

                    const std::uint32_t slot = mesh->Sections[sectionIndex].MaterialSlot;
                    const MaterialHandle materialHandle = slot < sectionMaterials->size()
                        ? (*sectionMaterials)[slot]
                        : sectionMaterials->back();
                    const Material* material = materials.Get(materialHandle);
                    if (material == nullptr || !material->CastShadows)
                        continue;

                    casters.Items.push_back(ShadowCasterItem{
                        .Mesh = renderer.Mesh,
                        .Material = materialHandle,
                        .SectionIndex = sectionIndex,
                        .WorldMatrix = worldMatrix,
                        .WorldBounds = worldBounds,
                        .DoubleSided = material->DoubleSided,
                    });
                }
            });
    }
}
