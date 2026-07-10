#pragma once

#include "MeshElementKind.h"
#include "selection/ISelectionContext.h" // SelectionSnapshot
#include "selection/SelectableRef.h"

#include <span>
#include <vector>

struct BrushMesh;

// Selected refs of one kind, grouped by owning entity.
struct EntityRefGroup
{
    EntityId Entity;
    std::vector<SelectableRef> Refs;
};

// Groups the selection's refs of `kind` by entity, in first-appearance order,
// except the primary ref's entity (when it matches the kind) leads. Invalid
// refs and other kinds are skipped. This is how the transform manipulators
// resolve an element selection spanning several meshes.
[[nodiscard]] std::vector<EntityRefGroup> GroupSelectionByEntity(const SelectionSnapshot& selection,
                                                                 SelectableKind kind);

// Re-expresses one brush's mesh-element refs in another element kind, so
// switching Face/Edge/Vertex mode carries the selection along instead of
// dropping it: a face maps to its boundary edges or corner vertices, an edge to
// its endpoints or incident faces, a vertex to its incident edges or faces.
// Refs of the target kind pass through; Object drops elements to a single
// entity ref. All refs must belong to the mesh's entity. Deduplicated, in
// first-encounter order.
[[nodiscard]] std::vector<SelectableRef> ConvertElementRefs(const BrushMesh& mesh,
                                                            std::span<const SelectableRef> refs,
                                                            MeshElementKind to);
