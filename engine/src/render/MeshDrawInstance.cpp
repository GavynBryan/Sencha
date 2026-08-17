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
        item.LightmapTextureIndex = instance.LightmapTextureIndex;
        item.AoTextureIndex = instance.AoTextureIndex;
        item.LightmapScaleBias = instance.LightmapScaleBias;
        queue.AddOpaque(item);
        ++emitted;
    }
    return emitted;
}
