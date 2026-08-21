#include <render/MeshDrawInstance.h>

#include <render/RenderQueue.h>
#include <render/SectionMaterial.h>

std::uint32_t EmitMeshSections(const MeshDrawInstance& instance,
                               const GpuStaticMesh& mesh,
                               std::span<const MaterialHandle> slotMaterials,
                               const MaterialCache& materials,
                               RenderQueue& queue)
{
    if (slotMaterials.empty())
        return 0;

    // A mesh with no lightmap UVs cannot sample its zone's atlas: every lookup
    // would land on texel (0,0). It has worked because the packer reserves that
    // texel black and the AO plane initialises white, so the sample happened to
    // contribute nothing -- correct by arrangement rather than by decision, and
    // only while both of those hold. Deciding it here also drops a per-fragment
    // texture fetch the shader was making to read a known-black texel.
    const std::uint32_t lightmapIndex =
        mesh.HasLightmapUvs ? instance.LightmapTextureIndex : UINT32_MAX;
    const std::uint32_t aoIndex =
        mesh.HasLightmapUvs ? instance.AoTextureIndex : UINT32_MAX;

    std::uint32_t emitted = 0;
    const auto sectionCount = static_cast<std::uint32_t>(mesh.Sections.size());
    for (std::uint32_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
    {
        if ((instance.SectionMask & (1u << sectionIndex)) == 0)
            continue;

        const MaterialHandle materialHandle =
            ResolveSectionMaterial(mesh, sectionIndex, slotMaterials);
        const Material* material = materials.Get(materialHandle);
        if (material == nullptr)
            continue;

        RenderQueueItem item{};
        item.Mesh = instance.Mesh;
        item.Material = materialHandle;
        item.SectionIndex = sectionIndex;
        item.WorldMatrix = instance.WorldMatrix;
        item.WorldBounds = instance.WorldBounds;
        item.CameraDepth = instance.CameraDepth;
        // Both come from the material, so they are view-independent and every
        // host gets them. The editor used to leave them at their defaults,
        // which drew unlit materials lit and double-sided materials
        // back-face culled in the viewport but not in game.
        item.Pass = material->Pass;
        item.Pipeline = SelectOpaquePipeline(*material);
        item.LightmapTextureIndex = lightmapIndex;
        item.AoTextureIndex = aoIndex;
        item.LightmapScaleBias = instance.LightmapScaleBias;
        // Blend is a different pass, not a pipeline bit: order replaces state
        // grouping as what the sort is for, so the item goes to the list whose
        // order is decided per view. The loader classified the pass at load
        // time; this routes on it. Mask stayed opaque -- it cuts fragments but
        // writes depth like anything else.
        item.Pass = material->Pass;
        if (item.Pass == ShaderPassId::ForwardTransparent)
            queue.AddTransparent(item);
        else
            queue.AddOpaque(item);
        ++emitted;
    }
    return emitted;
}
