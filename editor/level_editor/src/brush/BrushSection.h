#pragma once

#include "brush/BrushMesh.h"

#include <math/geometry/3d/Transform3d.h>
#include <math/Vec.h>

#include <vector>

// Where a horizontal plane cuts a placed brush, as outlines on the ground
// plane: x is world X, y is world Z. Outer outlines and holes alike, each in
// order around its edge; a brush with an open surface may leave open runs,
// which are not closed.
struct BrushSectionContour
{
    std::vector<Vec2d> Points;
    bool Closed = true;
};

// Empty when the plane misses the brush or the cut cannot be completed.
[[nodiscard]] std::vector<BrushSectionContour> SectionBrushAtHeight(const BrushMesh& mesh,
                                                                    const Transform3f& toWorld,
                                                                    float height);
