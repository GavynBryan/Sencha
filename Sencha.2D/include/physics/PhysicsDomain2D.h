#pragma once

#include <core/batch/SparseSet.h>
#include <entity/EntityHandle.h>
#include <math/geometry/2d/Aabb2d.h>
#include <math/spatial/QuadTree.h>
#include <math/Vec.h>
#include <physics/Collider2D.h>
#include <physics/CollisionGrid2D.h>
#include <physics/NarrowPhase2D.h>
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
// scratch each fixed step by RigidBodySyncSystem2D. Queries gather candidates
// from the tree then perform exact AABB tests.
//=============================================================================
class PhysicsDomain2D
{
public:
    explicit PhysicsDomain2D(const PhysicsConfig2D& config = {});

    // -- Collider registration ------------------------------------------------
    //
    // Register: add or replace a collider for an entity.
    // Unregister: remove the entity collider from the domain.
    // UpdateBounds: update the cached WorldBounds for an existing collider.

    bool Register(EntityHandle entity, const Collider2D& collider);
    bool Unregister(EntityHandle entity);
    void UpdateBounds(EntityHandle entity, const Aabb2d& worldBounds);
    bool Contains(EntityHandle entity) const;

    // Rebuild the broadphase tree from the current collider state.
    // Call once per step after all UpdateBounds calls are complete
    // (RigidBodySyncSystem2D does this at the end of its Tick).
    void RebuildTree();

    // -- Spatial queries ------------------------------------------------------

    // OverlapBox: fill 'out' with entities of all colliders whose WorldBounds
    // overlap 'box'. Clears 'out' before writing.
    void OverlapBox(const Aabb2d& box, std::vector<EntityHandle>& out) const;

    // SweepBox: sweep 'box' by 'delta', return first blocking hit.
    // Time is in [0,1] normalized along delta. DidHit=false if no blocker found.
    struct SweepHit
    {
        float        Time   = 1.0f;
        EntityHandle Entity;
        bool         DidHit = false;
    };
    SweepHit SweepBox(const Aabb2d& box, Vec2d delta) const;

    // MoveBox: move-and-slide. Resolves collisions axis-by-axis (X then Y)
    // against all registered colliders. Returns safe resolved delta and
    // contact surface flags. Pass the mover's entity as 'exclude' to
    // prevent self-collision.
    MoveResult2D MoveBox(const Aabb2d& box, Vec2d desiredDelta,
                         EntityHandle exclude = {}) const;

    // MoveProjected: iterative velocity-projection resolver for circle movers.
    // Intended for player characters that need smooth wall-sliding and corner
    // gliding. Queries both the collision grid (if set) and the dynamic
    // collider registry. Ghost edges on grid tile seams are suppressed
    // automatically via neighbor solid checks.
    //
    // center       — current world-space center of the circle
    // radius       — circle radius
    // delta        — desired movement this frame
    // exclude      — optional entity to skip (pass the mover's own entity if
    //                it is registered in the domain)
    //
    // Returns ResolvedDelta = actual movement; Hits = surfaces contacted.
    MoveResult2D MoveProjected(Vec2d center, float radius, Vec2d delta,
                               EntityHandle exclude = {}) const;

    // SetCollisionGrid: attach a static-geometry grid for MoveProjected to
    // query. Passing nullptr detaches any existing grid. The domain does NOT
    // take ownership — the caller must keep the grid alive.
    void SetCollisionGrid(const CollisionGrid2D* grid);

private:
    // -- Broadphase (AABB path) -----------------------------------------------

    void GatherCandidates(const Aabb2d& box,
                          std::vector<uint32_t>& out) const;

    float ResolveAxis(const Aabb2d& box, float delta, bool isVertical,
                      bool& hitPositive, bool& hitNegative,
                      EntityHandle exclude = {}) const;

    // -- Circle narrow-phase (projected path) ---------------------------------

    // Internal contact record: CircleContact from NarrowPhase2D plus the grid
    // cell that produced it (both -1 for dynamic collider contacts).
    struct DomainContact
    {
        CircleContact Base;
        int GridCol = -1;
        int GridRow = -1;
    };

    // Gather contacts from the collision grid into 'out'.
    void GatherGridContacts(Vec2d center, float radius, Vec2d velocity,
                            std::vector<DomainContact>& out) const;

    // Gather contacts from the dynamic collider registry into 'out'.
    // Colliders owned by 'exclude' are skipped.
    void GatherDomainContacts(Vec2d center, float radius, Vec2d velocity,
                              EntityHandle exclude,
                              std::vector<DomainContact>& out) const;

    // -- Data -----------------------------------------------------------------

    SparseSet<Collider2D> Colliders;

    QuadTree<uint32_t> Tree;

    const CollisionGrid2D* CollGrid = nullptr;

    // Reusable scratch vectors for broadphase queries. Declared mutable so
    // const query methods (MoveBox, SweepBox, OverlapBox) can reuse them
    // without per-call heap allocation. Safe because the physics domain is
    // single-threaded — queries never overlap.
    mutable std::vector<uint32_t> ScratchCandidates;
};
