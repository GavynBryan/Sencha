#pragma once

#include <ecs/EntityId.h>
#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/RenderEntityKey.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>

#include <cstdint>
#include <vector>

//=============================================================================
// Projected object shadows -- the plain records (spec §17).
//
// The grounding technique for things that move: render a caster's silhouette
// from its shadow direction into a small tile, then re-draw nearby static
// receivers with that tile projected onto them. These are the CPU-side facts
// the policies and passes exchange; nothing here names Vulkan.
//=============================================================================

// One object that grounds itself with a projected silhouette this frame.
// Per OBJECT, not per section, unlike ShadowCasterItem: the silhouette is one
// image of the whole thing, and its sections never diverge inside it.
struct ProjectedShadowCaster
{
    // Stable identity for direction smoothing and deterministic ordering.
    RenderEntityKey Key;
    SkinnedMeshHandle Mesh;
    Mat4 WorldMatrix = Mat4::Identity();
    Aabb3d WorldBounds = Aabb3d::Empty();
    std::uint32_t SectionMask = 0xFFFFFFFFu;
    // The smoothed direction the silhouette is rendered and projected along,
    // unit length, filled by the direction policy after extraction.
    Vec<3> Direction = Vec<3>(0.0f, -1.0f, 0.0f);
};

struct ProjectedShadowSet
{
    std::vector<ProjectedShadowCaster> Casters;

    void Reset() { Casters.clear(); }
};

// Retained per-entity smoothing state. The direction a caster grounds along
// must not pop when two lights swap dominance or when the caster crosses a
// range boundary, so each caster converges toward its target exponentially.
struct ProjectedShadowDirectionState
{
    RenderEntityKey Key;
    Vec<3> Direction = Vec<3>(0.0f, -1.0f, 0.0f);
    // Frames since the caster was last seen; state ages out rather than
    // accumulating for every entity that ever grounded.
    std::uint32_t UnseenFrames = 0;
};
