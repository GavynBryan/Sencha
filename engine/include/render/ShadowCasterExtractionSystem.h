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

// Per-section shadow-caster policy, shared by the runtime extractor and the
// editor's scene queues: a section casts when the mask admits it and its
// slot-resolved material exists with CastShadows set (last-member fallback
// for an under-bound material set, matching the forward queues). Material
// DoubleSided propagates onto the caster so the depth pass can select the
// no-cull pipeline.
void AppendShadowCasterSections(StaticMeshHandle meshHandle,
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
void AppendShadowCasters(const StaticMeshComponent& renderer,
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
