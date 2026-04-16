#pragma once

#include <math/geometry/2d/Aabb2d.h>
#include <math/Vec.h>

//=============================================================================
// Collider2D
//
// AABB-only collision shape for v0 physics. Defined in local space as a
// half-extent offset from the owning entity's world position.
//
// Offset allows the collider to be shifted relative to the transform origin
// (e.g. a character whose pivot is at the feet rather than the center).
//
// WorldBounds is computed each frame by ColliderSyncSystem2D from the entity's
// world Transform2D position + this shape description. Systems should treat
// WorldBounds as a read-only cache — write only through the shape fields or
// via the sync system.
//=============================================================================
struct Collider2D
{
    // -- Shape (local space, set once at creation) ----------------------------

    Vec2d HalfExtent = { 0.5f, 0.5f };
    Vec2d Offset     = { 0.0f, 0.0f };  // Center offset from transform position

    // -- Runtime cache (written by ColliderSyncSystem2D each frame) -----------

    Aabb2d WorldBounds = Aabb2d::Empty();

    // -- Flags ----------------------------------------------------------------

    bool IsStatic = false;  // True: participates as an immovable blocker only
};
