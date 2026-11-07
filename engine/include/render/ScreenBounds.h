#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>

// Where a world-space box lands on screen, in normalized device coordinates, x then y,
// each from -1 to 1. The rule every scissor built from bounds shares: a box wholly
// behind the eye covers nothing, and one straddling the eye plane has no meaningful
// projection, so it takes the whole view -- a larger rect costs fill, a smaller one
// clips what it should hold.
struct NdcBounds
{
    float Min[2] = { 1.0f, 1.0f };
    float Max[2] = { -1.0f, -1.0f };
    bool WholeView = false;

    [[nodiscard]] bool IsEmpty() const { return !WholeView && (Min[0] > Max[0] || Min[1] > Max[1]); }
    // Grows to hold `other` as well.
    void Include(const NdcBounds& other);
};

[[nodiscard]] NdcBounds ProjectToNdc(const Aabb3d& box, const Mat4& viewProjection);
