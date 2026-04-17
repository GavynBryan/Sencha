# Physics 2D

The 2D physics layer provides AABB collision detection, spatial queries, and move-and-slide resolution for kinematic actors. It is not a rigidbody simulator -- forces, torque, impulses, moving platforms, slopes, and friction are out of scope for v0.

`PhysicsDomain2D` is the authoritative spatial query surface. `ColliderSyncSystem2D` bridges the transform domain and the physics domain each frame. Neither is an `IService` -- `PhysicsDomain2D` is owned by `World2d::Physics`, and `ColliderSyncSystem2D` is a system registered through `PhysicsSetup2D::Setup`.

---

## Location

```
engine/include/physics/2d/Collider2D.h
engine/include/physics/2d/PhysicsDomain2D.h
engine/include/physics/2d/ColliderSyncSystem2D.h
engine/include/physics/2d/PhysicsSetup2D.h
engine/src/physics/2d/PhysicsDomain2D.cpp
engine/src/physics/2d/ColliderSyncSystem2D.cpp
engine/src/physics/2d/PhysicsSetup2D.cpp
```

```cpp
#include <physics/2d/PhysicsDomain2D.h>
#include <physics/2d/ColliderSyncSystem2D.h>
#include <physics/2d/PhysicsSetup2D.h>
```

---

## Core types

**`Collider2D`** is a local-space AABB shape. `HalfExtent` and `Offset` define the shape relative to the owning entity's transform origin. `WorldBounds` is a runtime cache written each frame by `ColliderSyncSystem2D` -- systems should treat it as read-only. `IsStatic` marks the collider as an immovable blocker.

**`PhysicsHandle2D`** is an opaque registration token returned by `PhysicsDomain2D::Register`. Callers pass it back to `UpdateBounds` and `Unregister`. Value 0 is the null handle.

**`PhysicsConfig2D`** holds construction-time parameters for `PhysicsDomain2D`. It controls the broadphase quadtree bounds, maximum depth, and entries-per-leaf threshold. Loaded from the `physics2d` section of `engine.json`.

**`MoveResult2D`** is the output of `MoveBox`. It carries the resolved movement delta and a `HitFlags2D` bitmask (`Hits`) with `HitFloor()`, `HitCeiling()`, `HitWall()` accessors.

---

## Broadphase

`PhysicsDomain2D` uses a `QuadTree<uint32_t>` for spatial partitioning. The tree stores slot indices into the internal collider array, not the colliders themselves.

The tree is rebuilt from scratch each frame via `RebuildTree()` -- `Clear()` followed by a linear insert of all live entries. There is no incremental update or remove on the tree. Full rebuild produces a tree that is always optimally subdivided for the current frame's collider distribution, avoiding the node degradation that accumulates from incremental removals.

---

## PhysicsDomain2D

### Collider registration

```cpp
PhysicsDomain2D& physics = world.Physics;

// Register a collider. Returns a handle the caller must hold.
Collider2D shape;
shape.HalfExtent = { 0.5f, 0.5f };
shape.IsStatic = true;
PhysicsHandle2D handle = physics.Register(shape);

// Push updated world bounds each frame (ColliderSyncSystem2D does this).
physics.UpdateBounds(handle, worldBounds);

// Rebuild the broadphase tree after all bounds are current.
physics.RebuildTree();

// Remove the collider.
physics.Unregister(handle);
```

### Spatial queries

```cpp
// Overlap: find all colliders intersecting a box.
std::vector<PhysicsHandle2D> hits;
physics.OverlapBox(queryBox, hits);

// Sweep: cast a box along a delta, find the first blocking hit.
// Time is [0,1] normalized along delta.
PhysicsDomain2D::SweepHit hit = physics.SweepBox(box, delta);
if (hit.DidHit) { /* hit.Time, hit.Handle */ }

// Move-and-slide: resolve collisions axis-by-axis (X then Y).
MoveResult2D result = physics.MoveBox(box, desiredDelta);
// result.ResolvedDelta -- safe movement
// result.HitFloor(), result.HitCeiling(), result.HitWall()
```

`SweepBox` uses Minkowski expansion and the slab method for swept AABB-vs-AABB tests. `MoveBox` resolves X first, shifts the box, then resolves Y -- standard axis-sequential depenetration.

---

## ColliderSyncSystem2D

The sync system bridges transforms and physics each frame:

1. For each registered collider, reads the entity's world `Transform2f`.
2. Computes the world-space AABB from `Collider2D::HalfExtent`, `Offset`, and the transform's position and scale.
3. Pushes updated bounds into `PhysicsDomain2D::UpdateBounds`.
4. Calls `RebuildTree()` once all bounds are current.

### PhysicsToken

Colliders are registered through `ColliderSyncSystem2D::AddCollider`, which returns a move-only `PhysicsToken`. The token is RAII -- when it destructs, the collider is automatically unregistered from both the sync system and the physics domain.

```cpp
ColliderSyncSystem2D& sync = systemHost.Get<ColliderSyncSystem2D>();

Collider2D shape;
shape.HalfExtent = { 0.5f, 1.0f };
shape.IsStatic = true;

// AddCollider links a collider to a transform key.
ColliderSyncSystem2D::PhysicsToken token =
    sync.AddCollider(transformKey, shape);

// Token is RAII -- destruction removes the collider.
// Or remove explicitly:
sync.RemoveCollider(token);

// Read back the live collider shape (e.g. to adjust at runtime).
Collider2D* col = sync.TryGetCollider(token);
```

---

## Setup

`PhysicsSetup2D::Setup` registers `ColliderSyncSystem2D` and orders it after `TransformPropagationSystem` so world transforms are current before AABBs are computed.

```cpp
PhysicsSetup2D::Setup(serviceHost, systemHost);
```

Call once during engine initialization, after `WorldSetup::Setup2D`. Game-specific movement systems (motors, kinematic controllers) are added from game code after this call, using `World2d::Physics` for spatial queries.

---

## Frame ordering

```
TransformPropagationSystem  (PostUpdate, low order)
  -> ColliderSyncSystem2D   (PostUpdate, higher order)
       reads world transforms
       computes world AABBs
       pushes UpdateBounds
       calls RebuildTree
  -> [game movement systems] (query PhysicsDomain2D for MoveBox / SweepBox / OverlapBox)
```

---

## Configuration

`PhysicsConfig2D` fields can be set in `engine.json`:

```json
{
    "physics2d": {
        "tree_max_depth": 6,
        "tree_max_entries_per_leaf": 8,
        "tree_bounds": {
            "min_x": -1024,
            "min_y": -1024,
            "max_x": 1024,
            "max_y": 1024
        }
    }
}
```

`TreeBounds` should encompass the entire playable area. Colliders outside these bounds still work (they land at the root node) but broadphase quality degrades. `TreeMaxDepth` of 6-8 is typical for 2D platformers.

---

## Constraints

**v0 is AABB-only.** Rotation is not applied to collider shapes. `ColliderSyncSystem2D` reads scale from the transform but ignores rotation -- colliders stay axis-aligned in world space.

**One kinematic mover at a time.** `MoveBox` resolves a single box against all registered colliders. Multiple simultaneous movers require separate `MoveBox` calls and do not interact with each other.

**`PhysicsToken` must not outlive `ColliderSyncSystem2D`.** The token stores a raw pointer to the sync system. Destroy all tokens before tearing down the system.

**`PhysicsDomain2D` is not an `IService`.** Resolve it through `World2d::Physics`, not through `ServiceHost`.

---

## Relationship to the rest of the engine

```
ColliderSyncSystem2D        transform -> AABB bridge; owns PhysicsTokens
       |  reads TransformView<Transform2f> for world positions
       |  pushes UpdateBounds + RebuildTree into PhysicsDomain2D
       |
PhysicsDomain2D             collider registry + spatial queries
       |  broadphase: QuadTree<uint32_t> (slot indices)
       |  narrowphase: AABB overlap, Minkowski sweep, axis depenetration
       |
World2d::Physics            ownership; not a standalone service
       |
PhysicsSetup2D::Setup       wires ColliderSyncSystem2D after TransformPropagation
```
