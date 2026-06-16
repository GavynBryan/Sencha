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
    [[nodiscard]] static std::optional<BrushState> TryGetState(const LevelScene& scene, EntityId entity);

    [[nodiscard]] static Aabb3d ComputeBounds(const BrushState& state);
    [[nodiscard]] static std::array<Vec3d, 8> ComputeCorners(const BrushState& state);

    [[nodiscard]] static BrushState Translate(const BrushState& state, Vec3d delta);
};
