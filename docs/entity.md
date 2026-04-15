# Entity

The entity system layers stable game-object identity on top of the transform
hierarchy.  An `EntityBatch<T>` stores typed game structs in a `DataBatch<T>`
and registers each item with `EntityRegistry` on emplacement.  Destruction goes
through the registry so any entity — regardless of its concrete type — can be
torn down by key, including full subtree destruction that walks the transform
hierarchy leaves-first.

---

## Location

```
engine/include/world/entity/EntityKey.h
engine/include/world/entity/EntityRecord.h
engine/include/world/entity/EntityRegistry.h
engine/include/world/entity/IsEntity.h
engine/include/world/entity/EntityBatch.h
engine/src/world/entity/EntityRegistry.cpp
```

```cpp
#include <world/entity/EntityBatch.h>
#include <world/entity/EntityKey.h>
#include <world/entity/EntityRegistry.h>
```

`EntityRegistry` lives inside `World<TTransform>` as the `Entities` member.
Gameplay code does not instantiate it directly.

---

## Core types

**`EntityKey`** is the stable handle to an entity.  It wraps a `DataBatchKey`
so it carries generation information — a stale key stops resolving after the
entity is destroyed and its slot is reused.  Default-constructed `EntityKey` is
null: `operator bool()` returns false.

**`EntityRecord`** is per-entity metadata stored inside `EntityRegistry`.  It
holds the entity's transform key (for hierarchy lookup) and a type-erased
`OnDestroy` callback so the registry can tear down any entity type without
knowing its concrete type.  `Owner` / `OwnerSlot` / `OnDestroy` form a triad
that `EntityBatch<T>` fills in on each `Emplace`.

**`EntityRegistry`** is the central registry mapping `EntityKey` →
`EntityRecord`.  It issues stable keys, maintains a reverse transform-key →
entity-key map, and invokes `OnDestroy` callbacks.  The registry removes the
`EntityRecord` **before** invoking `OnDestroy` so that RAII teardown inside the
callback (e.g. `TransformHierarchyRegistration`) observes a clean registry
state.

**`IsEntity`** is a C++ concept satisfied by any type that exposes
`TransformKey() const -> DataBatchKey`.  It constrains `EntityBatch<T>` so only
types with a valid transform key can be stored as entities.

**`EntityBatch<T>`** wraps `DataBatch<T>` and auto-wires registration with
`EntityRegistry` on `Emplace`.  It is non-copyable and non-movable because it
stores `this` as the `Owner` pointer in each `EntityRecord`; moving would
silently dangle those pointers.

---

## IsEntity conformance

Any struct that embeds a `TransformNode` satisfies `IsEntity` by forwarding its
`TransformKey()`:

```cpp
struct Goblin
{
    TransformNode2d Node;
    // ... gameplay state

    DataBatchKey TransformKey() const { return Node.TransformKey(); }
};
```

The `TransformNode` member manages hierarchy registration via RAII.  No extra
work is needed.

---

## API

### EntityBatch<T>

```cpp
// Construction — binds to the world's entity registry.
EntityBatch<Goblin> Goblins{ world.Entities };

// Emplace — registers with EntityRegistry and returns a stable EntityKey.
EntityKey ek = Goblins.Emplace(world.Domain, Transform2f{});

// Cold-path lookup by EntityKey.
Goblin* g = Goblins.TryGet(ek);         // nullptr if stale or wrong batch

// Hot-path iteration — direct span, no registry involvement.
for (Goblin& g : Goblins.GetItems())    { /* ... */ }

size_t n = Goblins.Count();
bool   empty = Goblins.IsEmpty();
```

### EntityRegistry

```cpp
// Usually reached through World::Entities or World::DestroySubtree.

// Destroy a single entity (removes record, then invokes OnDestroy).
world.Entities.Destroy(ek);

// Destroy an entity and all transform-hierarchy descendants, leaves first.
world.Entities.DestroySubtree(ek, world.Domain.Hierarchy);

// Convenience wrapper on World — passes Domain.Hierarchy automatically.
world.DestroySubtree(ek);

// Queries
const EntityRecord* rec = world.Entities.Find(ek);
EntityKey fromTransform = world.Entities.FindByTransform(transformKey);
bool live = world.Entities.IsRegistered(ek);
size_t n  = world.Entities.Count();

// Remove record only — no OnDestroy callback.
// Use Destroy() for normal teardown; Unregister() is for cases where the
// owning batch is already tearing down the item through its own path.
world.Entities.Unregister(ek);
```

---

## Idiomatic setup

### Single entity type

```cpp
struct Goblin
{
    TransformNode2d Node;
    int             Health = 100;

    Goblin(TransformDomain<Transform2f>& domain, const Transform2f& local)
        : Node(domain, local) {}

    DataBatchKey TransformKey() const { return Node.TransformKey(); }
};

// Declare the batch once, bound to the world's registry.
EntityBatch<Goblin> Goblins{ world.Entities };

// Spawn a goblin.
EntityKey ek = Goblins.Emplace(world.Domain, Transform2f({ 100.0f, 50.0f }));

// Propagate transforms then read world position.
world.Domain.Propagation.Propagate();
const Transform2f* t = world.Domain.Transforms.TryGetWorld(
    Goblins.TryGet(ek)->TransformKey());

// Destroy the goblin.
world.Entities.Destroy(ek);
```

### Parent-child subtree destruction

```cpp
struct Rider { TransformNode2d Node; DataBatchKey TransformKey() const { return Node.TransformKey(); } };
struct Horse  { TransformNode2d Node; DataBatchKey TransformKey() const { return Node.TransformKey(); } };

EntityBatch<Horse>  Horses{ world.Entities };
EntityBatch<Rider>  Riders{ world.Entities };

EntityKey horseKey = Horses.Emplace(world.Domain, Transform2f{{ 200.0f, 0.0f }});
EntityKey riderKey = Riders.Emplace(world.Domain, Transform2f{{   0.0f, 40.0f }});

// Parent the rider to the horse via the transform hierarchy.
Riders.TryGet(riderKey)->Node.SetParent(Horses.TryGet(horseKey)->Node);

// DestroySubtree removes the rider first (leaf), then the horse (root).
world.DestroySubtree(horseKey);
// Both EntityKeys are now stale.
```

### Hot-path iteration

`EntityBatch<T>::GetItems()` returns a contiguous span of T with no registry
involvement.  Use it for every per-frame sweep.

```cpp
// Physics update — no EntityKey needed.
for (Goblin& g : Goblins.GetItems())
    ApplyGravity(g);

// Read world transforms for rendering.
for (const Goblin& g : Goblins.GetItems())
{
    const Transform2f* t = world.Domain.Transforms.TryGetWorld(g.TransformKey());
    if (t) SubmitSprite(t->Position);
}
```

---

## Constraints

**`EntityBatch<T>` must outlive all `EntityKey`s it issued.**  The registry
holds a raw pointer to the batch as `Owner`.  If the batch is destroyed while
records are still registered, `Destroy()` will call a dangling `OnDestroy`.
Destroy all entities before tearing down the batch, or ensure the batch lives
for the full scene lifetime.

**`EntityBatch<T>` is non-copyable and non-movable.**  The `Owner` pointer
stored in each `EntityRecord` points to `this`.  A move would silently dangle
every outstanding record.

**Do not call `EntityRegistry::Unregister` from inside `OnDestroy`.**  The
registry clears the record and reverse-map entry **before** invoking the
callback.  A second `Unregister` call from inside the callback will no-op
safely (the key is already gone), but it indicates a double-teardown bug and
should be fixed.

**`DestroySubtree` collects the full post-order before any destruction
begins.**  Hierarchy modifications during teardown (e.g. a child's destructor
reparenting a grandchild) do not affect the collected order.  Nodes that
disappear from the registry between collection and destruction are silently
skipped.

**Every entity requires a non-null transform key.**  `EntityRegistry::Register`
asserts that `record.TransformKey.Value != 0`.  Registering an entity whose
`TransformNode` was not yet emplaced into a `TransformStore` will fire that
assertion.

**Do not use `EntityRegistry` as a general-purpose object store.**  It is
specifically for objects that participate in the transform hierarchy and need
cross-type destroy routing.  Plain game objects with no parenting requirements
should use `DataBatch<T>` directly.

---

## Relationship to the rest of the engine

```
EntityBatch<T>            typed game struct container; auto-registers on Emplace
       │  Emplace → EntityRegistry::Register(EntityRecord)
       │
EntityRegistry            key issuance, reverse transform map, OnDestroy dispatch
       │  DestroySubtree → TransformHierarchyService::GetChildren (post-order walk)
       │  Destroy → record removed → OnDestroy(owner, slot) → DataBatch::RemoveKey
       │
TransformHierarchyService parent-child graph; read by DestroySubtree to collect order
       │
DataBatch<T>              dense storage for the entity struct; slot freed by OnDestroy
       │
World<TTransform>         owns EntityRegistry + TransformDomain; exposes DestroySubtree
```
