#pragma once

#include <core/assets/AssetRef.h>
#include <render/static_mesh/MeshGeometry.h>
#include <render/static_mesh/StaticMeshVertex.h>

#include <span>
#include <string>
#include <vector>

//=============================================================================
// Brush geometry cook (docs/plans/sencha-level-editor/05-level-cook.md §2).
// Dev-only — compiled under SENCHA_ENABLE_COOK. Pure: no logging, no threads,
// no disk (only the level-cook writer touches disk).
//
// The single brush→static-mesh bake, used by BOTH the offline level cook and
// PIE ("one path, two schedulings"). It deliberately consumes faces that are
// ALREADY triangulated into world space: the editor fills them straight from
// BrushTessellate (editor/level/brush/BrushTessellation.h), so the cooked mesh
// is built from the exact triangles the editor previewed — cooked geometry ==
// preview by construction, not by a parallel reimplementation. (This is why the
// bake takes baked triangles rather than the plan's literal CookBrush+UvProjection:
// the projection/triangulation already live in BrushTessellate, and duplicating
// them here is precisely the divergence risk the shared kernel exists to avoid.)
//
// The bake's own job — what the editor must NOT duplicate — is the static-mesh
// shape: grouping faces into one section per material, welding duplicate
// vertices, generating MikkTSpace tangents, and computing bounds.
//=============================================================================

// One brush face's contribution: its (resolved) material and the triangles it
// tessellated to, in world space, fan order, 3*N vertices. Tangent is ignored on
// input — the bake produces it from the baked UVs.
struct CookFace
{
    AssetRef                      Material;  // resolved: the level default is already applied
    std::vector<StaticMeshVertex> Triangles; // world space, 3 per triangle
};

// The distinct materials across `faces`, in first-seen order. This is the
// section/slot order: baked section N and the cooked StaticMeshComponent's
// material slot N both name materialOrder[N], so they cannot disagree.
[[nodiscard]] std::vector<AssetRef> CollectMaterialOrder(std::span<const CookFace> faces);

// Bake brush faces into ONE MeshGeometry: faces grouped into one StaticMeshSection
// per material (MaterialSlot = index in materialOrder), exact-duplicate vertices
// welded per section, MikkTSpace tangents generated from the baked UVs, section
// and mesh bounds computed. A material present in materialOrder but unused emits
// no section. Returns false with `error` set if a face's material is absent from
// materialOrder.
[[nodiscard]] bool BakeBrushFacesToStaticMesh(std::span<const CookFace> faces,
                                              std::span<const AssetRef> materialOrder,
                                              MeshGeometry& out,
                                              std::string* error = nullptr);
