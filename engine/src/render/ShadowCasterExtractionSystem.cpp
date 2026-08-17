#include <render/ShadowCasterExtractionSystem.h>

#include <world/transform/TransformHistory.h>

#include <math/geometry/3d/AabbTransform.h>
#include <render/RenderEntityKey.h>

namespace
{

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
                                      TransformAabb(mesh.LocalBounds, worldMatrix),
                                      casters);
}

void ShadowCasterExtractionSystem::Extract(
    const World& world,
    const StoragePartitionSet& partitions,
    const StaticMeshCache& meshes,
    const MaterialCache& materials,
    const MaterialSetCache& materialSets,
    ShadowCasterSet& casters,
    bool emitRecords,
    double interpolationAlpha)
{
    casters.Reset();

    if (!world.IsRegistered<WorldTransform>()
        || !world.IsRegistered<StaticMeshComponent>())
    {
        return;
    }

    if (LastWorld != &world || !CachedQuery.has_value())
    {
        CachedQuery.emplace(world);
        CachedInterpolatedQuery.emplace(world);
        LastWorld = &world;
    }

    // Casters must use the same pose their mesh renders at, or a shadow
    // separates from the object dropping it.
    const auto emitChunk = [&](auto& view, auto&& poseAt)
    {
        const auto renderers = view.template Read<StaticMeshComponent>();

        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            const StaticMeshComponent& renderer = renderers[i];
            if (!renderer.Visible || !renderer.CastShadows)
                continue;

            const GpuStaticMesh* mesh = meshes.Get(renderer.Mesh);
            const std::vector<MaterialHandle>* sectionMaterials =
                materialSets.Get(renderer.Materials);
            if (mesh == nullptr || sectionMaterials == nullptr)
                continue;

            const ShadowCasterGatherResult gathered = AppendShadowCasters(
                renderer, *mesh, *sectionMaterials, materials,
                poseAt(i).ToMat4(), casters);
            if (gathered.EffectiveSectionMask == 0 || !emitRecords)
                continue;

            casters.Records.push_back(ShadowCasterRecord{
                .Key = RenderEntityKey{ .Entity = view.Entity(i) },
                .State = ShadowCasterState{
                    .WorldBounds = QuantizeShadowCasterBounds(gathered.WorldBounds),
                    .Mesh = renderer.Mesh,
                    .Materials = renderer.Materials,
                    .EffectiveShadowSectionMask = gathered.EffectiveSectionMask,
                    .ShadowMaterialStateHash = gathered.MaterialStateHash,
                },
            });
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
