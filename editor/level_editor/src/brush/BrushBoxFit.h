#pragma once

#include "brush/BrushMesh.h"

#include <math/Quat.h>
#include <math/Vec.h>

#include <optional>
#include <span>
#include <string>

struct OrientedBox
{
    Vec3d Center = Vec3d::Zero();
    Quatf Orientation = Quatf::Identity();
    Vec3d HalfExtents = Vec3d::Zero();
};

// The box a brush mesh exactly is, from its vertices in the frame the box is
// wanted in: six quads in three perpendicular parallel pairs, every corner on
// it. Otherwise null, with `reason` saying what kept it from being one.
[[nodiscard]] std::optional<OrientedBox> FitOrientedBox(const BrushMesh& mesh,
                                                        std::span<const Vec3d> positions,
                                                        std::string& reason);
