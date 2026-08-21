#pragma once

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <render/ProjectedShadowTypes.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <optional>

//=============================================================================
// ProjectedShadowExtractionSystem
//
// Gathers the objects grounding themselves with a projected silhouette this
// frame. Its own system rather than a widening of ShadowCasterExtraction:
// that one is per-SECTION and feeds light shadow maps through a different
// authored flag; this one is per-OBJECT and feeds the silhouette pass. The
// two participations are independent facts, and folding them together is the
// mode-enum shape the participation seam exists to avoid.
//
// Camera-independent, like the light-map caster gather: the set is gathered
// once per frame, and each view's frame policy ranks and budgets from it.
//=============================================================================
// The per-instance rule, pure so the flag's policy is testable without a
// World or a resident cache: a mesh grounds itself when it is visible and
// asked to, and its record carries bounds derived from the mesh at the pose
// it renders with. Returns whether a caster was appended.
bool AppendProjectedShadowCaster(const SkinnedMeshComponent& renderer,
                                 const GpuStaticMesh& mesh,
                                 const Mat4& worldMatrix,
                                 RenderEntityKey key,
                                 ProjectedShadowSet& out);

class ProjectedShadowExtractionSystem
{
public:
    void Extract(const World& world,
                 const StoragePartitionSet& partitions,
                 const SkinnedMeshCache& meshes,
                 ProjectedShadowSet& out,
                 double interpolationAlpha = 1.0);

private:
    const World* LastWorld = nullptr;
    std::optional<Query<Read<WorldTransform>,
                        Read<SkinnedMeshComponent>,
                        Without<WorldTransformHistory>>> CachedQuery;
    std::optional<Query<Read<WorldTransformHistory>,
                        Read<SkinnedMeshComponent>>> CachedInterpolatedQuery;
};
