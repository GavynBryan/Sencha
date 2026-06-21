#pragma once

#include "LevelScene.h"

#include <math/geometry/3d/Aabb3d.h>

#include <optional>

class BrushGeometry
{
public:
    // True world AABB of a brush: every vertex transformed, min/max. Offset-aware
    // (unlike a pivot-symmetric box), and the one bounds the selection box, the
    // bounds gizmo, and create-from-selection all share.
    [[nodiscard]] static Aabb3d ComputeWorldBounds(const BrushMesh& mesh, const Transform3f& transform);
    [[nodiscard]] static std::optional<Aabb3d> ComputeWorldBounds(const LevelScene& scene, EntityId entity);
};
