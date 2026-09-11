#pragma once

#include <math/Vec.h>

#include <vector>

// Carve shapes are outline generators over a drag box, not carve algorithms.
// The box is the tool's state and the outline is derived from it, so changing
// shape or a parameter re-previews from the box the user already drew instead
// of asking them to draw it again.

enum class CarveShape
{
    Rectangle,
    Arch,
};

struct CarveShapeParams
{
    float ArchRise = 0.5f; // fraction of the box height the arch springs over, 0 to 1
    int ArchSegments = 8;  // arc segments; the arc carries ArchSegments + 1 points
    // Radians. Positive is a right-handed rotation about the face's outward
    // normal, which because AxisU x AxisV == Normal is the ordinary
    // counter-clockwise turn in the frame's own (U, V) and reads as
    // counter-clockwise from outside the face. A positive quarter turn carries
    // the apex from +AxisV round to -AxisU.
    float Orientation = 0.0f;
};

struct CarveShapeLimits
{
    int MinSegments = 2;
    int MaxSegments = 2;
    // The rise the generator is defined across: 0 is the rectangle and 1 springs
    // from the floor. Reported here so the properties slider and the viewport
    // drag clamp against one range instead of two literals that can drift.
    float MinRise = 0.0f;
    float MaxRise = 1.0f;
    // No arch is representable: the arc's shortest chord would weld away at
    // every segment count. Only happens for a rise near zero, where the arch is
    // the rectangle anyway.
    [[nodiscard]] bool Empty() const { return MaxSegments < MinSegments; }
};

// The segment counts an arch of this size and rise can actually carry. At least
// two, so the arch has an apex; at most the count whose shortest chord still
// exceeds `weldTol`, since a shorter one would be welded away and leave a
// vertex the outline did not ask for.
//
// Validation lives here rather than in the generator: the generator honours its
// parameters exactly, and the tool clamps against this range so the properties
// panel can show what it clamped to.
[[nodiscard]] CarveShapeLimits CarveShapeRange(Vec2d boxMin, Vec2d boxMax, float rise,
                                              float orientation, float weldTol);

// Where a shape sits inside its box: the centre, the half-extents the turn
// leaves it, and the turn itself. The generator builds outlines in this frame,
// and the tool needs the same one to put a handle on the springline and to read
// a dragged handle back out as a rise.
struct CarveShapeFrame
{
    Vec2d Center;
    float SemiU = 0.0f; // across the shape, before the turn
    float SemiV = 0.0f; // along its rise, before the turn
    float Sin = 0.0f;
    float Cos = 1.0f;

    [[nodiscard]] Vec2d ToBox(Vec2d local) const
    {
        return Vec2d{ Center.X + local.X * Cos - local.Y * Sin,
                      Center.Y + local.X * Sin + local.Y * Cos };
    }
    [[nodiscard]] Vec2d ToShape(Vec2d box) const
    {
        const Vec2d d{ box.X - Center.X, box.Y - Center.Y };
        return Vec2d{ d.X * Cos + d.Y * Sin, -d.X * Sin + d.Y * Cos };
    }
    // The springline's height in this frame at `rise`: the top at rise 0, the
    // floor at rise 1.
    [[nodiscard]] float SpringlineAt(float rise) const
    {
        return rise >= 1.0f ? -SemiV : SemiV - 2.0f * SemiV * rise;
    }
};

[[nodiscard]] CarveShapeFrame CarveShapeFrameFor(Vec2d boxMin, Vec2d boxMax, float orientation);

// Half-extents of the largest-area rectangle that fits inside a box of
// half-extents (p, q) once it is turned by `angle` about its centre. Both
// components are strictly positive for positive inputs; a degenerate box
// returns zero.
//
// This is where a turned shape gets its room. Maximizing a*b subject to the
// turned rectangle's own upright extent fitting,
//
//     a*c + b*s <= p        a*s + b*c <= q        c = |cos|, s = |sin|
//
// gives the box itself at a quarter turn -- (q, p), which turned back covers the
// box exactly -- so a quarter turn leaves an opening alone and only turns what is
// cut out of it. Off the axes the rectangle is smaller than the box, which is
// drawn, so a turned shape reads as smaller rather than as one that quietly left
// its bounds.
[[nodiscard]] Vec2d LargestInscribedRotatedRectangle(float p, float q, float angle);

// The outline of `shape` over the box, counter-clockwise and closed implicitly.
//
// The arch is the upper half of an ellipse with semi-axes `width/2` and
// `rise * height`, on jambs below the springline. A circular segment cannot do
// this job: for chord w and rise h its radius is w^2/(8h) + h/2, so past
// h = w/2 it becomes a major arc bulging outside the jambs, and a tall narrow
// doorway could not reach even half its rise without leaving the box. The
// ellipse stays inside the box at every aspect ratio and every rise, rise 0 is
// the rectangle, rise 1 springs from the floor, and a true circular arch is
// still reachable at rise = width / (2 * height).
[[nodiscard]] std::vector<Vec2d> CarveShapeOutline(CarveShape shape, Vec2d boxMin, Vec2d boxMax,
                                                   const CarveShapeParams& params);
