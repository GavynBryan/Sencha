#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/ShadowCasterSet.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <cstdint>
#include <span>

struct Registry;

// Per-instance gather summary, feeding the caster diff: which sections
// actually cast after material filtering, the shadow-relevant material state
// hash (per-section cast and double-sided flags only), and the instance's
// world bounds.
struct ShadowCasterGatherResult
{
    std::uint32_t EffectiveSectionMask = 0;
    std::uint64_t MaterialStateHash = 0;
    Aabb3d WorldBounds = Aabb3d::Empty();
};

// Per-section shadow-caster policy, shared by the runtime extractor and the
// editor's scene queues: a section casts when the mask admits it and its
// slot-resolved material exists with CastShadows set (last-member fallback
// for an under-bound material set, matching the forward queues). Material
// DoubleSided propagates onto the caster so the depth pass can select the
// no-cull pipeline. The returned bounds echo `worldBounds`.
ShadowCasterGatherResult AppendShadowCasterSections(
    StaticMeshHandle meshHandle,
    const GpuStaticMesh& mesh,
    std::span<const MaterialHandle> sectionMaterials,
    const MaterialCache& materials,
    std::uint32_t sectionMask,
    const Mat4& worldMatrix,
    const Aabb3d& worldBounds,
    ShadowCasterSet& casters);

// One mesh instance's caster gather: honors the component's Visible and
// CastShadows switches, derives world bounds from the mesh bounds, and
// appends the instance's casting sections. Camera-independent by contract:
// casters occlude light views, not the observer's.
ShadowCasterGatherResult AppendShadowCasters(
    const StaticMeshComponent& renderer,
    const GpuStaticMesh& mesh,
    std::span<const MaterialHandle> sectionMaterials,
    const MaterialCache& materials,
    const Mat4& worldMatrix,
    ShadowCasterSet& casters);

class ShadowCasterExtractionSystem
{
public:
    void Extract(std::span<Registry*> registries,
                 const StaticMeshCache& meshes,
                 const MaterialCache& materials,
                 const MaterialSetCache& materialSets,
                 ShadowCasterSet& casters) const;
};
