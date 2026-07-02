#pragma once

#include <math/Vec.h>

#include <optional>

// Transient pivot for the transform gizmos. Override, when set, replaces the
// computed selection pivot, so move/rotate/scale act about it. Editing retargets
// the Move gizmo to move the pivot itself instead of the selection. Both reset
// when the selection changes; neither is serialized or undoable.
struct PivotState
{
    std::optional<Vec3d> Override;
    bool Editing = false;
};
