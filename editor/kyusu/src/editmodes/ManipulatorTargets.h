#pragma once

#include "IManipulator.h" // ManipulatorContext

#include "brush/BrushMesh.h"
#include "meshedit/MeshElementKind.h"
#include "selection/SelectableRef.h"

#include <math/geometry/3d/Transform3d.h>

#include <optional>
#include <vector>

// Shared selection-resolution for the transform manipulators (translate, rotate,
// scale). Each reads the same selection the same way; the apply (how the delta is
// used) differs per manipulator, the gathering does not.

// One selected entity and its pre-drag transform.
struct ObjectTarget
{
    EntityId Entity;
    Transform3f Initial;
};

// Every selected entity that resolves to a transform, with its pre-drag state.
[[nodiscard]] std::vector<ObjectTarget> GatherObjectTargets(const ManipulatorContext& ctx);

// One entity's share of the active-mode element selection: its mesh, transform,
// and the selected refs of that kind on it.
struct ElementTarget
{
    EntityId Entity;
    BrushMesh Mesh;
    Transform3f Transform;
    std::vector<SelectableRef> Elements;
};

// Every entity with selected elements of the active kind that resolves to a
// mesh, primary's entity first, then first-appearance order. Empty when
// nothing of that kind resolves. Single-target consumers (extrude) take the
// front, which is the primary's mesh.
[[nodiscard]] std::vector<ElementTarget> ResolveElementTargets(const ManipulatorContext& ctx, MeshElementKind kind);
