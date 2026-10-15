#pragma once

#include <math/Vec.h>

// One world-space line segment with a screen-space pixel width. Unlike the 1px
// LINE_LIST colored-vertex pipeline, the wide-line pipeline expands each segment into a
// camera-facing quad, so strokes have an exact pixel width and analytic AA.
// Also the GPU instance record: the vertex shader expands each segment into six
// vertices, so the upload is one of these per segment (no CPU expansion).
struct EditorLineSegment
{
    Vec3d A;
    Vec3d B;
    Vec4  Color;
    float WidthPx = 1.0f; // full stroke width in pixels
};
