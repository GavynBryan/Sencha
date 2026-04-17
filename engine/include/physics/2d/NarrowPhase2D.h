#pragma once

#include <math/geometry/2d/Aabb2d.h>
#include <math/Vec.h>
#include <physics/2d/CollisionGrid2D.h>

//=============================================================================
// NarrowPhase2D — pure geometric collision primitives
//
// Free functions with no domain or world state. Each function takes geometry
// in and returns a result out. Suitable for direct unit testing.
//=============================================================================

//-----------------------------------------------------------------------------
// CircleContact
//
// Result of a swept-circle narrow-phase test. TOI is normalized to [0, 1]
// along the velocity vector; TOI > 1 means no contact within the frame.
// Normal is outward from the contacted surface toward the circle center.
//-----------------------------------------------------------------------------
struct CircleContact
{
    float TOI    = 2.0f; // > 1.0 = no hit
    Vec2d Normal = {};
};

//-----------------------------------------------------------------------------
// SweepCircleVsAabb
//
// Sweeps a circle (center + radius) along 'velocity' against a static AABB.
// Tests the four expanded face planes and the four corner arcs of the
// Minkowski sum. Returns the earliest entry contact within [0, 1];
// contact.TOI > 1 if the velocity vector clears the shape entirely.
//
// Normal convention: points from the AABB surface toward the circle.
// Velocity projection formula: v -= dot(v, normal) * normal
//-----------------------------------------------------------------------------
CircleContact SweepCircleVsAabb(Vec2d center, float radius,
                                Vec2d velocity, const Aabb2d& aabb);

//-----------------------------------------------------------------------------
// IsGhostEdge
//
// Returns true when 'surfaceNormal' at grid cell (col, row) points toward a
// neighboring solid cell, making the edge interior to the solid geometry.
// Interior edges must be suppressed to prevent a circle from snagging on
// tile seams as it slides along a wall.
//
// Face normals: checks the single neighbor in the normal's direction.
// Corner normals (both components non-trivial): suppressed if either
// face-adjacent neighbor is solid.
//-----------------------------------------------------------------------------
bool IsGhostEdge(Vec2d surfaceNormal, const CollisionGrid2D& grid,
                 int col, int row);
