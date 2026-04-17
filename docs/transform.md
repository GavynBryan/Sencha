# Transform

`Transform2d<T>` and `Transform3d<T>` are TRS (translation, rotation, scale)
value types.  Together with the hierarchy and propagation services they form the
engine's scene-graph: local transforms are authored per-object, world transforms
are derived automatically, and the hot propagation loop does no allocation or
recursion.

---

## Location

```
engine/include/math/geometry/2d/Transform2d.h
engine/include/math/geometry/3d/Transform3d.h
engine/include/world/transform/
```

```cpp
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/transform/TransformNode.h>
#include <world/transform/TransformSpace.h>
```

Common aliases are `Transform2f = Transform2d<float>` and
`Transform3f = Transform3d<float>`.

---

## Struct layout

### 2D

```cpp
template <typename T>
struct Transform2d
{
    Vec<2, T> Position;   // translation
    T         Rotation;   // angle in radians, counter-clockwise
    Vec<2, T> Scale;      // non-uniform scale per axis

    static Transform2d Identity();   // { (0,0), 0, (1,1) }
};
```

### 3D

```cpp
template <typename T>
struct Transform3d
{
    Vec<3, T> Position;   // translation
    Quat<T>   Rotation;   // unit quaternion
    Vec<3, T> Scale;      // non-uniform scale per axis

    static Transform3d Identity();   // { (0,0,0), identity quat, (1,1,1) }
};
```

### Composition

Both types expose `operator*` for parent-then-child composition:

```cpp
Transform2f world = parent * local;
```

Scale components multiply component-wise.  Rotation composes additively (2D) or
via quaternion multiplication (3D).  The child's position is scaled by the
parent's scale, then rotated by the parent's rotation, then offset by the
parent's position.

---

## Hierarchy

`TransformHierarchyService` owns the parent-child graph.  It is a plain data
structure — it records relationships but does not update transforms itself.

```
engine/include/world/transform/TransformHierarchyService.h
```

```cpp
// Attach / detach
hierarchy.SetParent(childKey, parentKey);   // parent child; auto-registers both
hierarchy.ClearParent(childKey);            // promote child to root

// Manual registration (optional — SetParent registers implicitly)
hierarchy.Register(key);
hierarchy.Unregister(key);                  // orphans all children (they become roots)

// Query
bool hasParent  = hierarchy.HasParent(key);
bool hasKids    = hierarchy.HasChildren(key);
auto parent     = hierarchy.GetParent(key);    // returns optional key
auto& children  = hierarchy.GetChildren(key);  // std::vector<DataBatchKey>
auto& roots     = hierarchy.GetRoots();
```

Every structural change (register, unregister, reparent) increments a
`VersionCounter`.  The propagation cache reads this counter to decide whether to
rebuild.  No callers need to manage the counter directly.

---

## Propagation

`TransformPropagationSystem<TTransform>` derives world transforms from local
transforms in a single forward pass.

```
engine/include/world/transform/TransformPropagationSystem.h
```

```cpp
propagation.Propagate();
```

The rules are:

```
root       →  world = local
child of P →  world = world[P] * local
```

Internally the system maintains a cached flat order emitted by BFS from the
roots.  Parents always appear before their children.  On each `Propagate()` call
the cache is validated against the hierarchy and batch version counters; it is
rebuilt only when stale.  The hot loop is a straight forward pass over the flat
order: no hash lookups, no recursion, no allocation.

**World transforms are write-only from the propagation system's perspective.**
Outside code reads them but must not write them.  Modify the local transform to
reposition an object; the next `Propagate()` call will derive the new world
transform.

---

## TransformSpace

`TransformSpace<TTransform>` is a self-contained bundle of all transform
services.  Any subsystem that needs its own isolated transform space creates one.

```
engine/include/world/transform/TransformSpace.h
```

```cpp
TransformSpace<Transform2f> domain;

// Contained services (all accessible as members):
domain.Transforms   // TransformStore — allocates and owns local/world pairs
domain.Hierarchy    // TransformHierarchyService — parent-child graph
domain.Propagation  // TransformPropagationSystem — derives world transforms
```

`World<TTransform>` owns one `TransformSpace` and one `EntityRegistry` for
game-world objects.  UI, editor gizmos, and particle systems can own
independent `TransformSpace` instances with no coupling between them.

---

## API

### TransformStore

```
engine/include/world/transform/TransformStore.h
```

```cpp
// Allocate a transform slot (local + world share the same key)
DataBatchHandle<TTransform> handle = domain.Transforms.Emplace(localTransform);
DataBatchKey key = handle.GetToken();

// Keyed access
const TTransform* local = domain.Transforms.TryGetLocal(key);
const TTransform* world = domain.Transforms.TryGetWorld(key);
      TTransform* local = domain.Transforms.TryGetLocal(key);  // mutable

// Bulk span (system sweeps — no key arithmetic in the loop)
std::span<const TTransform> locals = domain.Transforms.GetLocalsSpan();
std::span<const TTransform> worlds = domain.Transforms.GetWorldsSpan();
```

### TransformNode

`TransformNode<TTransform>` is a reusable composition primitive for any object
that participates in the hierarchy.

```
engine/include/world/transform/TransformNode.h
```

```cpp
TransformNode2d node(domain, initialLocal);

// Parenting
node.SetParent(parentNode);           // accepts TransformNode or raw key
node.SetParent(parentKey);
node.ClearParent();
bool parented = node.HasParent();
auto parentKey = node.GetParentKey(); // returns optional

// Key for passing to other services
DataBatchKey key = node.TransformKey();
```

`TransformNode` is move-constructible (move transfers ownership cleanly) and
copy-deleted.  It registers with the hierarchy on construction and unregisters
on destruction via `TransformHierarchyRegistration`.

---

## Idiomatic setup

### Single unparented object

```cpp
TransformNode2d obj(world.Domain, Transform2f({ 100.0f, 50.0f }, 0.0f, { 1.0f, 1.0f }));
world.Domain.Propagation.Propagate();

const Transform2f* t = world.Domain.Transforms.TryGetWorld(obj.TransformKey());
// t->Position == (100, 50)
```

### Parent-child pair

```cpp
TransformNode2d parent(world.Domain, Transform2f({ 200.0f, 0.0f }, 0.0f, { 2.0f, 2.0f }));
TransformNode2d child(world.Domain,  Transform2f({  10.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
child.SetParent(parent);

world.Domain.Propagation.Propagate();

const Transform2f* cw = world.Domain.Transforms.TryGetWorld(child.TransformKey());
// cw->Position == (220, 0)  — child local (10, 0) scaled by parent (x2) then offset by (200, 0)
// cw->Scale    == (2, 2)    — parent scale inherited
```

### Three-level hierarchy

```cpp
TransformNode2d root(world.Domain, Transform2f({ 100.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
TransformNode2d mid(world.Domain,  Transform2f({  50.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
TransformNode2d leaf(world.Domain, Transform2f({  10.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));

mid.SetParent(root);
leaf.SetParent(mid);

world.Domain.Propagation.Propagate();

const Transform2f* lw = world.Domain.Transforms.TryGetWorld(leaf.TransformKey());
// lw->Position == (160, 0)  — 100 + 50 + 10
```

### Integration into a struct

Types that own a transform slot follow the same pattern as `TransformNode`
internally: allocate a slot on construction, register with the hierarchy, expose
a key for parenting.

```cpp
struct Actor
{
    Actor(TransformSpace<Transform2f>& domain, const Transform2f& local)
        : Node(domain, local)
    {}

    void SetParent(const Actor& parent)   { Node.SetParent(parent.Node); }
    DataBatchKey TransformKey() const     { return Node.TransformKey(); }

private:
    TransformNode2d Node;
};
```

### System sweep (world transforms)

```cpp
// Apply world positions to a physics broadphase.
std::span<const Transform2f> worlds = world.Domain.Transforms.GetWorldsSpan();
for (const Transform2f& t : worlds)
    broadphase.UpdateAABB(t.Position, t.Scale);
```

---

## Constraints

**Always write the local transform; never write the world transform.**  World
transforms are outputs of `Propagate()`.  Writing directly to a world transform
will be silently overwritten on the next propagation pass.

**Call `Propagate()` once per frame, after all local-transform mutations.**
Reading a world transform before `Propagate()` has run for the current frame
yields the value from the previous frame.  Systems that consume world transforms
must be ordered after the propagation step.

**Do not skip registration.**  A key that is added to a `TransformStore` but
never registered with the hierarchy is treated as orphaned and will be absent
from the propagation order.  Use `TransformNode` (which registers automatically)
or call `hierarchy.Register(key)` manually.

**`Unregister` strictly enforces explicit teardown.**  Removing a parent key from the hierarchy
will trigger an assertion failure if that parent still has active children.
Because entities are composites without inheritance, the engine does not know how to
recursively destroy them safely. If a parent should be removed, the application must
query `GetChildren()` and explicitly unregister or tear down the children first,
preventing accidental orphaning and memory leaks.

**Do not store world position inside the transform's owning struct.**  Caching
the world transform in a member variable creates a shadow copy that diverges
silently.  Read world transforms from `TryGetWorld` or the world span.

**Do not reparent inside the propagation loop.**  Modifying the hierarchy during
`Propagate()` invalidates the cache mid-pass.  Collect reparent operations and
apply them before the `Propagate()` call.

**Scale composes multiplicatively.**  A child with local scale `(0.5, 0.5)` under
a parent with world scale `(4, 4)` has a world scale of `(2, 2)`.  There is no
mechanism to opt out of inherited scale; a child that must ignore a parent's
scale must compensate explicitly in its own local transform.

---

## Relationship to the scene-graph pipeline

The transform system is the foundation beneath every positioned object.  The
layers compose without any reaching downward past their direct dependency:

```
Transform2d / Transform3d     pure TRS value; no ownership, no allocation
        │
TransformStore                allocates paired local/world slots per key
        │
TransformHierarchyService     records parent-child relationships; version-tracked
        │
TransformPropagationOrder     caches BFS-ordered flat pass; rebuilt on version change
        │
TransformPropagationSystem    executes forward pass: worlds[i] = worlds[parent] * locals[i]
        │
TransformNode / Tilemap2d     per-object wrappers; own a slot + RAII registration
        │
EntityBatch<T> / EntityRegistry   typed entity containers + cross-type destroy routing
        │
World<TTransform>             owns the domain + EntityRegistry; exposes it to gameplay
```

Objects (actors, tilemaps, UI elements, particles) sit at the `TransformNode` /
`EntityBatch` layer.  They author local transforms and read world transforms.
Everything below that line is infrastructure.
