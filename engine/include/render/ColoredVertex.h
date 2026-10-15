#pragma once

#include <math/Vec.h>

// A world-space point with a linear RGBA colour: what immediate-mode lines and
// flat fills are drawn from.
struct ColoredVertex
{
    Vec3d Position;
    Vec4 Color;
};
