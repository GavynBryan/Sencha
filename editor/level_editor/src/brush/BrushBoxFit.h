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

// The box a brush mesh is, if it is exactly one: six four-sided faces in three
// pairs, each pair parallel and every pair perpendicular to the others, every
// corner on the box. `positions` are the mesh's vertices in the frame the box
// is wanted in. Otherwise null, with `reason` saying what kept it from being one.
[[nodiscard]] std::optional<OrientedBox> FitOrientedBox(const BrushMesh& mesh,
                                                        std::span<const Vec3d> positions,
                                                        std::string& reason);
