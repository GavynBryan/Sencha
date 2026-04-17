#pragma once

#include <math/geometry/2d/Aabb2d.h>
#include <math/spatial/QuadTree.h>
#include <math/Vec.h>
#include <physics/2d/Collider2D.h>
#include <cstdint>
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
    // Bounds of the quadtree root node. Should encompass the entire playable
    // area. Colliders outside these bounds are still handled (they land at the
    // root) but broadphase quality degrades.
    Aabb2d TreeBounds = Aabb2d(Vec2d(-1024.0f, -1024.0f), Vec2d(1024.0f, 1024.0f));

    // Maximum quadtree subdivision depth. Higher = tighter cells but more
    // nodes to traverse. 6-8 is typical for 2D platformers.
    int TreeMaxDepth = 6;

    // Maximum entries in a leaf before it subdivides.
    int TreeMaxEntriesPerLeaf = 8;
};

//=============================================================================
// MoveResult2D
//
// Output of PhysicsDomain2D::MoveBox. Carries the resolved movement delta and
// which surfaces were contacted during resolution.
//=============================================================================
enum class HitFlags2D : uint8_t
{
    None    = 0,
    Floor   = 1 << 0, // Contacted a surface below (down direction)
    Ceiling = 1 << 1, // Contacted a surface above (up direction)
    Wall    = 1 << 2, // Contacted a left or right surface
};

constexpr HitFlags2D operator|(HitFlags2D a, HitFlags2D b)
{
    return static_cast<HitFlags2D>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr HitFlags2D& operator|=(HitFlags2D& a, HitFlags2D b) { a = a | b; return a; }
constexpr bool HasFlag(HitFlags2D mask, HitFlags2D flag)
{
    return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(flag)) != 0;
}

struct MoveResult2D
{
    Vec2d ResolvedDelta = { 0.0f, 0.0f }; // Actual movement after depenetration
    HitFlags2D Hits = HitFlags2D::None;

    bool HitFloor()   const { return HasFlag(Hits, HitFlags2D::Floor); }
    bool HitCeiling() const { return HasFlag(Hits, HitFlags2D::Ceiling); }
    bool HitWall()    const { return HasFlag(Hits, HitFlags2D::Wall); }
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
// Spatial partitioning uses a QuadTree broadphase. The tree is rebuilt from
// scratch each frame (or after level geometry changes) by
// ColliderSyncSystem2D. Queries gather candidates from the tree then perform
// exact AABB tests.
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

    // Rebuild the broadphase tree from the current collider state.
    // Call once per frame after all UpdateBounds calls are complete
    // (ColliderSyncSystem2D does this at the end of its Update).
    void RebuildTree();

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

    // -- Broadphase ------------------------------------------------------------

    void GatherCandidates(const Aabb2d& box,
                          std::vector<uint32_t>& out) const;

    // Single-axis depenetration. Returns safe travel distance along the axis.
    // hitPositive / hitNegative: set true if stopped in that direction.
    float ResolveAxis(const Aabb2d& box, float delta, bool isVertical,
                      bool& hitPositive, bool& hitNegative) const;

    // -- Data -----------------------------------------------------------------

    std::vector<ColliderEntry> Entries;
    std::vector<uint32_t>      FreeSlots;
    uint32_t                   NextHandle = 1; // 0 is null

    QuadTree<uint32_t> Tree;

    // Reusable scratch vectors for broadphase queries. Declared mutable so
    // const query methods (MoveBox, SweepBox, OverlapBox) can reuse them
    // without per-call heap allocation. Safe because the physics domain is
    // single-threaded — queries never overlap.
    mutable std::vector<uint32_t> ScratchCandidates;
};
