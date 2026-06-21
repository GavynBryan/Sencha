#pragma once

#include "LevelScene.h"

#include <math/geometry/3d/Aabb3d.h>

#include <array>
#include <optional>

// Transient axis-aligned-box view of a brush, used by the create-drag preview and
// whole-brush body bounds/move. Editable geometry is the BrushMesh; this is a
// convenience box derived from its local bounds.
struct BrushState
{
    Transform3f Transform = Transform3f::Identity();
    Vec3d HalfExtents = { 0.5, 0.5, 0.5 };
};

class BrushGeometry
{
public:
    // True world AABB of a brush: every vertex transformed, min/max. Offset-aware
    // (unlike a pivot-symmetric box), and the one bounds the selection box, the
    // bounds gizmo, and create-from-selection all share.
    [[nodiscard]] static Aabb3d ComputeWorldBounds(const BrushMesh& mesh, const Transform3f& transform);
    [[nodiscard]] static std::optional<Aabb3d> ComputeWorldBounds(const LevelScene& scene, EntityId entity);

    // Corners of the symmetric create-drag preview box (centered on the transform).
    [[nodiscard]] static std::array<Vec3d, 8> ComputeCorners(const BrushState& state);
};
