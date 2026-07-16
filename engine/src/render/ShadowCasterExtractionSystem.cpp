#include <render/ShadowCasterExtractionSystem.h>

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

    constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    void HashByte(std::uint64_t& hash, std::uint8_t value)
    {
        hash ^= value;
        hash *= kFnvPrime;
    }
}

ShadowCasterGatherResult AppendShadowCasterSections(
    StaticMeshHandle meshHandle,
    const GpuStaticMesh& mesh,
    std::span<const MaterialHandle> sectionMaterials,
    const MaterialCache& materials,
    std::uint32_t sectionMask,
    const Mat4& worldMatrix,
    const Aabb3d& worldBounds,
    ShadowCasterSet& casters)
{
    ShadowCasterGatherResult result;
    result.MaterialStateHash = kFnvOffset;
    result.WorldBounds = worldBounds;
    if (sectionMaterials.empty())
        return result;

    for (std::uint32_t sectionIndex = 0;
         sectionIndex < static_cast<std::uint32_t>(mesh.Sections.size());
         ++sectionIndex)
    {
        if ((sectionMask & (1u << sectionIndex)) == 0)
            continue;

        const std::uint32_t slot = mesh.Sections[sectionIndex].MaterialSlot;
        const MaterialHandle materialHandle = slot < sectionMaterials.size()
            ? sectionMaterials[slot]
            : sectionMaterials.back();
        const Material* material = materials.Get(materialHandle);
        if (material == nullptr)
            continue;

        // Every resolvable masked section feeds the hash, so a section whose
        // material stops or starts casting reads as a state change even
        // before the mask difference is considered.
        HashByte(result.MaterialStateHash, static_cast<std::uint8_t>(sectionIndex));
        HashByte(result.MaterialStateHash, material->CastShadows ? 1u : 0u);
        HashByte(result.MaterialStateHash, material->DoubleSided ? 1u : 0u);

        if (!material->CastShadows)
            continue;

        result.EffectiveSectionMask |= 1u << sectionIndex;
        casters.Items.push_back(ShadowCasterItem{
            .Mesh = meshHandle,
            .Material = materialHandle,
            .SectionIndex = sectionIndex,
            .WorldMatrix = worldMatrix,
            .WorldBounds = worldBounds,
            .DoubleSided = material->DoubleSided,
        });
    }
    return result;
}

ShadowCasterGatherResult AppendShadowCasters(
    const StaticMeshComponent& renderer,
    const GpuStaticMesh& mesh,
    std::span<const MaterialHandle> sectionMaterials,
    const MaterialCache& materials,
    const Mat4& worldMatrix,
    ShadowCasterSet& casters)
{
    if (!renderer.Visible || !renderer.CastShadows)
        return {};

    return AppendShadowCasterSections(renderer.Mesh, mesh, sectionMaterials, materials,
                                      renderer.SectionMask, worldMatrix,
                                      TransformBounds(mesh.LocalBounds, worldMatrix),
                                      casters);
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
                if (transform == nullptr || mesh == nullptr || sectionMaterials == nullptr)
                    return;

                const ShadowCasterGatherResult gathered = AppendShadowCasters(
                    renderer, *mesh, *sectionMaterials, materials,
                    transform->Value.ToMat4(), casters);
                if (gathered.EffectiveSectionMask == 0)
                    return;

                casters.Records.push_back(ShadowCasterRecord{
                    .Key = MakeRenderEntityKey(*registry, entity),
                    .State = ShadowCasterState{
                        .WorldBounds = QuantizeShadowCasterBounds(gathered.WorldBounds),
                        .Mesh = renderer.Mesh,
                        .Materials = renderer.Materials,
                        .EffectiveShadowSectionMask = gathered.EffectiveSectionMask,
                        .ShadowMaterialStateHash = gathered.MaterialStateHash,
                    },
                });
            });
    }
}
