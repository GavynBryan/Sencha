#pragma once

#include <render/Material.h>
#include <render/static_mesh/GpuStaticMesh.h>

#include <cstdint>
#include <span>

//=============================================================================
// Which material a mesh section draws with
//
// A section names a slot, and the instance supplies a material per slot. The
// rule matters more than its three lines: a slot past the end of the set falls
// back to the last entry, which is what keeps an under-bound material set
// drawing instead of vanishing.
//
// It has one home because two consumers must agree. Mesh extraction resolves it
// to pick the pipeline; shadow-caster extraction resolves it to read CastShadows
// and DoubleSided. If they resolved differently, an object would cast its shadow
// from a material it does not render with -- an inconsistency with no symptom
// until someone notices a two-sided object throwing a one-sided shadow.
//
// Returns an invalid handle for an empty set or a section index past the mesh.
//=============================================================================
// The slot a section draws from, by the same fallback; zero for an empty set.
[[nodiscard]] inline std::uint32_t ResolveSectionSlot(const GpuStaticMesh& mesh, std::uint32_t sectionIndex,
                                                     std::size_t slotCount)
{
    const std::uint32_t slot = mesh.Sections[sectionIndex].MaterialSlot;
    return slotCount == 0 || slot < slotCount ? slot : static_cast<std::uint32_t>(slotCount - 1);
}

[[nodiscard]] inline MaterialHandle ResolveSectionMaterial(
    const GpuStaticMesh& mesh,
    std::uint32_t sectionIndex,
    std::span<const MaterialHandle> slotMaterials)
{
    if (slotMaterials.empty() || sectionIndex >= mesh.Sections.size())
        return MaterialHandle{};

    return slotMaterials[ResolveSectionSlot(mesh, sectionIndex, slotMaterials.size())];
}
