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

// The active-mode element selection resolved to one entity, its mesh, transform,
// and the refs of that kind (the entity is the primary's if it matches, else the
// first matching ref). nullopt when nothing of that kind resolves.
struct ElementTarget
{
    EntityId Entity;
    BrushMesh Mesh;
    Transform3f Transform;
    std::vector<SelectableRef> Elements;
};

[[nodiscard]] std::optional<ElementTarget> ResolveElementTarget(const ManipulatorContext& ctx, MeshElementKind kind);
