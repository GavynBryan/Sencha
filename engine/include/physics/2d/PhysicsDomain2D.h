#pragma once

#include <math/geometry/2d/Aabb2d.h>
#include <math/Vec.h>
#include <physics/2d/Collider2D.h>
#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// PhysicsConfig2D
//
// Construction-time parameters for PhysicsDomain2D. Passed in by value at
// startup — not mutated at runtime. Loaded from the "physics2d" section of
// engine.json via DeserializePhysicsConfig2D.
//=============================================================================
struct PhysicsConfig2D
{
    // World-unit size of each spatial grid cell. Should be >= the largest
    // expected collider for best broadphase performance.
    float GridCellSize = 4.0f;
};

//=============================================================================
// MoveResult2D
//
// Output of PhysicsDomain2D::MoveBox. Carries the resolved movement delta and
// which surfaces were contacted during resolution.
//=============================================================================
struct MoveResult2D
{
    Vec2d ResolvedDelta = { 0.0f, 0.0f }; // Actual movement after depenetration

    bool HitFloor   = false; // Contacted a surface below (down direction)
    bool HitCeiling = false; // Contacted a surface above (up direction)
    bool HitWall    = false; // Contacted a left or right surface
};

//=============================================================================
// PhysicsHandle2D
//
// Opaque registration token returned by PhysicsDomain2D::Register.
// Callers must pass this back to UpdateBounds and Unregister.
// Value of 0 is the null/invalid handle.
//=============================================================================
struct PhysicsHandle2D
{
    uint32_t Value = 0;
    bool IsValid() const { return Value != 0; }
    bool operator==(const PhysicsHandle2D&) const = default;
};

//=============================================================================
// PhysicsDomain2D
//
// Authoritative spatial query layer for 2D physics in v0. Owns the collider
// registry and performs overlap, sweep, and move-and-slide resolution. Scoped
// and owned by World2d — not an IService.
//
// v0 scope:
//   - One moving (kinematic) actor, arbitrarily many static blockers
//   - AABB-only shapes
//   - Wall / floor / ceiling blocking
//
// Not a full rigidbody simulator — forces, torque, impulses, moving platforms,
// slopes, and friction are explicitly out of scope for v0.
//
// Spatial partitioning uses a simple uniform grid. Colliders are inserted into
// all cells their AABB overlaps. Queries gather candidates from relevant cells
// then perform exact AABB tests.
//
// Grid parameters are fixed per RebuildGrid call. The grid is rebuilt each
// frame (or after level geometry changes) by ColliderSyncSystem2D.
//=============================================================================
class PhysicsDomain2D
{
public:
    explicit PhysicsDomain2D(const PhysicsConfig2D& config = {});

    // -- Collider registration ------------------------------------------------
    //
    // Register: add a collider to the domain. WorldBounds must be valid on
    // first call. Returns a PhysicsHandle2D that the caller owns.
    //
    // Unregister: remove a collider. The handle becomes invalid immediately.
    // UpdateBounds: update the cached WorldBounds for an existing registration.

    PhysicsHandle2D Register(const Collider2D& collider);
    void            Unregister(PhysicsHandle2D handle);
    void            UpdateBounds(PhysicsHandle2D handle, const Aabb2d& worldBounds);

    // Rebuild the spatial grid from the current collider state.
    // Call once per frame after all UpdateBounds calls are complete
    // (ColliderSyncSystem2D does this at the end of its Update).
    void RebuildGrid();

    // -- Spatial queries ------------------------------------------------------

    // OverlapBox: fill 'out' with handles of all colliders whose WorldBounds
    // overlap 'box'. Clears 'out' before writing.
    void OverlapBox(const Aabb2d& box, std::vector<PhysicsHandle2D>& out) const;

    // SweepBox: sweep 'box' by 'delta', return first blocking hit.
    // Time is in [0,1] normalized along delta. DidHit=false if no blocker found.
    struct SweepHit
    {
        float           Time   = 1.0f;
        PhysicsHandle2D Handle;
        bool            DidHit = false;
    };
    SweepHit SweepBox(const Aabb2d& box, Vec2d delta) const;

    // MoveBox: move-and-slide. Resolves collisions axis-by-axis (X then Y)
    // against all registered colliders. Returns safe resolved delta and
    // contact surface flags.
    MoveResult2D MoveBox(const Aabb2d& box, Vec2d desiredDelta) const;

private:
    // -- Internal collider slot -----------------------------------------------

    struct ColliderEntry
    {
        Collider2D      Shape;
        PhysicsHandle2D Handle;
        bool            Live = false; // false = slot is free
    };

    // -- Grid -----------------------------------------------------------------

    struct GridCell
    {
        std::vector<uint32_t> SlotIndices; // indices into Entries
    };

    void GetCellRange(const Aabb2d& box,
                      int& minCX, int& maxCX,
                      int& minCY, int& maxCY) const;

    void GatherCandidates(const Aabb2d& box,
                          std::vector<uint32_t>& out) const;

    // Single-axis depenetration. Returns safe travel distance along the axis.
    // hitPositive / hitNegative: set true if stopped in that direction.
    float ResolveAxis(const Aabb2d& box, float delta, bool isVertical,
                      bool& hitPositive, bool& hitNegative) const;

    // -- Data -----------------------------------------------------------------

    float CellSize;
    int   GridWidth   = 0;
    int   GridHeight  = 0;
    float GridOriginX = 0.0f;
    float GridOriginY = 0.0f;

    std::vector<ColliderEntry> Entries;
    std::vector<uint32_t>      FreeSlots;
    uint32_t                   NextHandle = 1; // 0 is null

    std::vector<GridCell> Grid;
};
