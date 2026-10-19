#pragma once

#include "BrushMesh.h"

#include <math/Quat.h>
#include <math/geometry/3d/Transform3d.h>

#include <optional>
#include <string>

struct BrushBox
{
    Vec3d Center;
    Quatf Rotation;
    Vec3d HalfExtents;
};

// Accepts an exact closed six-quad box, including offset and rotated geometry.
// Other geometry fails rather than substituting its bounding box.
[[nodiscard]] std::optional<BrushBox> ExtractBrushBox(
    const BrushMesh& mesh, const Transform3f& transform, std::string& error);
