#pragma once

#include "brush/BrushMesh.h"
#include "selection/SelectableRef.h"

#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>
#include <vector>

//=============================================================================
// ElementGeometry — per-ref mesh primitives that dispatch on the ref's OWN kind
// exactly once (the single legitimate dispatch). Callers that used to switch on
// the active MeshElementKind in parallel (vertex gather, pivot, translate) now
// loop over refs and call these, so adding an element kind touches one case here
// instead of N scattered switches. Pure; reuses MeshElements.
//=============================================================================

// Local vertex indices a single ref references:
//   Vertex -> { ElementId }   Edge -> { A, B }   Face -> Loop   Entity -> {}
// Out-of-range indices are dropped; the caller dedups across refs.
[[nodiscard]] std::vector<std::uint32_t> ElementVertexIndices(const BrushMesh& mesh,
                                                              const Transform3f& transform,
                                                              const SelectableRef& ref);

// World-space representative point of a single MESH-element ref:
//   Vertex -> position   Edge -> midpoint   Face -> center   Entity -> nullopt
// (Object/entity pivots come from the transform, which is the sink's job, not
// this mesh-only primitive's.)
[[nodiscard]] std::optional<Vec3d> ElementCenter(const BrushMesh& mesh,
                                                 const Transform3f& transform,
                                                 const SelectableRef& ref);
