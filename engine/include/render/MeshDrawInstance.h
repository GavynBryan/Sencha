#pragma once

#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/AabbTransform.h>
#include <render/Material.h>
#include <render/MaterialCache.h>
#include <render/static_mesh/GpuStaticMesh.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>
#include <span>

class RenderQueue;

//=============================================================================
// MeshDrawInstance
//
// One renderable resolved down to the facts a queue item needs, before it is
// expanded into per-section items.
//
// Hosts resolve instances in completely different ways -- an ECS chunk walk
// over WorldTransform, the same walk over interpolated pose history, an editor
// entity list, a cooked scene snapshot, a baked brush at identity -- and then
// every one of them expands the result identically. The gather is per host;
// the expansion is EmitMeshSections, once.
//
// Fields are named for what they are rather than where they came from: an
// instance does not know whether its matrix came from a transform component or
// a brush bake.
//
// On the mesh handle, because the name invites the question: `Mesh` is a
// StaticMeshHandle because RenderQueueItem::Mesh is one, and that is where the
// constraint actually lives. "Static" here is the geometry kind -- rigid and
// non-deforming, as opposed to skinned -- and says nothing about whether the
// instance moves. The expansion itself is already kind-agnostic: it reads
// GpuStaticMesh, which is the shared residency of both kinds (its own header
// says so, and UploadMeshGeometryToGpu serves StaticMeshCache and
// SkinnedMeshCache alike).
//
// The second kind exists now, and the generalization is the one this comment
// predicted: the skinned handle rides beside the static one and discriminates
// the residency to resolve through. Exactly one of the two is valid per
// instance. At rest pose the skinned entry's rest geometry -- a GpuStaticMesh
// like any other -- serves the draw; a posed path replaces where the vertices
// come from, not this expansion.
//=============================================================================
struct MeshDrawInstance
{
    StaticMeshHandle Mesh;
    // Valid instead of Mesh for a skinned instance; the item resolves through
    // SkinnedMeshCache. Never both.
    SkinnedMeshHandle SkinnedMesh;
    Mat4 WorldMatrix = Mat4::Identity();
    Aabb3d WorldBounds = Aabb3d::Empty();

    // One bit per section; a clear bit hides that section.
    std::uint32_t SectionMask = 0xFFFFFFFFu;

    // View-relative depth for the opaque sort. Hosts that build one queue and
    // replay it under several cameras leave this at zero: a single depth cannot
    // be correct for more than one view.
    float CameraDepth = 0.0f;

    // Bindless slots of the zone's baked atlas and AO plane, UINT32_MAX for an
    // instance with neither.
    std::uint32_t LightmapTextureIndex = UINT32_MAX;
    std::uint32_t AoTextureIndex = UINT32_MAX;
    Vec4 LightmapScaleBias = Vec4{ 1.0f, 1.0f, 0.0f, 0.0f };
};

// Appends one opaque item per section `instance.SectionMask` selects, resolving
// each section's material through its slot. Returns how many were emitted.
//
// A section whose slot is past the end of `slotMaterials` falls back to the
// last entry, which is how an under-bound material set still draws. A section
// whose material is not in the cache is skipped: the pass would drop it at
// record time anyway, and Pass and Pipeline cannot be resolved without it.
std::uint32_t EmitMeshSections(const MeshDrawInstance& instance,
                               const GpuStaticMesh& mesh,
                               std::span<const MaterialHandle> slotMaterials,
                               const MaterialCache& materials,
                               RenderQueue& queue);
