# Sencha ECS: Overview

This is the first document to read. It explains the core concepts, how they fit together,
and walks through writing a new component and system from scratch.

---

## Why an archetype ECS?

Sencha uses an **archetype entity-component system** for game-object data. The alternative
— storing each component type in its own flat array (sparse-set storage) — has a ceiling
that matters for real scenes:

- Iterating "entities that have A *and* B" requires crossing from one array to another per
  entity. At 50k+ entities this dominates frame time.
- Archetype storage groups entities by their *exact component signature*. Iterating any
  combination of components on the same archetype chunk is a single linear sweep.
- Archetype chunks are a natural unit for cache-friendly access: one 16 KB slab fits in L1.

The migration from sparse-set storage is documented in `docs/ecs/decisions.md`.

---

## Core concepts

### EntityId

A generational handle: `struct EntityId { uint32_t Index; uint32_t Generation; }`.

`Index` is the slot in the entity registry. `Generation` distinguishes reuses of the same
slot. Storage only reads the index; generation is checked at API boundaries (`TryGet`,
`IsAlive`). A default-constructed `EntityId` with `Index == UINT32_MAX` is invalid.

### ComponentId

A small integer (`uint16_t`) assigned when a component type is registered. Stable for the
lifetime of a `World`. Up to 256 component types per world (the v1 budget).

### ArchetypeSignature

A `std::bitset<256>` where bit *i* is set if the component with `ComponentId i` is
present. Two entities are in the same archetype iff their signatures are equal. Bitset
AND/equality compiles to four 64-bit comparisons.

### Chunk

A 16 KB block of memory that holds rows of one archetype. Memory layout inside a chunk:

```
[column 0: capacity × stride0 bytes]
[column 1: capacity × stride1 bytes]
...
[entity indices: capacity × 4 bytes]   ← always last
```

Each column is a contiguous array of one component type (SoA, not AoS). The entity-index
column is last so component sweeps don't pollute cache lines with entity metadata.

Capacity (rows per chunk) = `16384 / (sum of component strides + 4)`. For a
`{LocalTransform, WorldTransform}` archetype (each 40 bytes) capacity is 195 rows.

### Archetype

Metadata for one component signature: the column layout, rows-per-chunk, and the list of
live chunks. Two or more chunks exist only when the first chunk is full.

### World

Owns the entity registry, all archetypes, resources, and the structural-change guard.
Every ECS operation goes through `World`. There is no global state; each zone owns a
`World` instance.

### Query

A durable, cached object parameterized by accessor types. Caches the list of matching
archetypes and rebuilds lazily when new archetypes are created. Hot path: zero hash-map
probes, zero dynamic dispatch. See `docs/ecs/queries.md`.

### CommandBuffer

Records structural mutations (add/remove component, create/destroy entity) during system
execution. Flushed at explicit phase boundaries by the scheduler. See
`docs/ecs/command-buffers.md`.

### ComponentTraits\<T\>

Opt-in specialization point for lifecycle hooks (`OnAdd`, `OnRemove`). Default
specialization is trivial — zero overhead for components without hooks. See
`docs/ecs/component-traits.md`.

### Resources

Singleton objects owned by `World`, keyed by type. Used for per-world state that is not
attached to any entity: the camera frustum, the frame clock, the propagation order cache,
asset caches.

---

## Structural changes

An entity's **archetype** is determined by its exact component signature. Adding or
removing a component moves the entity to a different archetype. This is called a
*structural change*.

**Structural changes are never allowed mid-query.** All structural mutations from inside
a system go through a `CommandBuffer` and flush at explicit scheduler phase boundaries.
Calling `World::AddComponent` or `World::DestroyEntity` while a query is active is an
assertion failure in debug builds.

During startup and teardown — before any query is active — `World` structural methods are
called directly.

---

## Tag components

A tag component is a zero-size struct. It contributes only a signature bit; it has no
per-entity data column and takes no chunk space:

```cpp
struct TagStatic {};   // zero-size: is_empty_v<TagStatic> == true
struct TagPlayer {};
```

Tags are appropriate for stable facts that persist for many frames. They are **not**
appropriate for transient per-frame state (visible-this-frame, just-moved, dirty). For
that use `Changed<T>`. Overusing tags as per-frame flags causes archetype churn: entities
move between archetypes twice per frame just to flip a bit.

---

## Change detection

Each chunk stores a per-column **last-written-frame counter**. When a `Write<T>` query
finishes iterating a chunk, the counter for column T is bumped to the current frame
(conservative bump — regardless of whether any row was actually written).

`Changed<T>` in a query signature filters out chunks whose T column was not bumped since
the reference frame. This lets downstream systems skip large chunks of unchanged data.

**Conservative semantics**: a chunk that matched `Write<T>` but whose rows were not
actually modified will still pass a `Changed<T>` filter. Systems using `Changed<T>` must
tolerate false positives at chunk granularity.

---

## Writing a new component

**Step 1 — define the struct** near the feature that owns it:

```cpp
// engine/include/gameplay/Health.h
#pragma once

struct Health
{
    float Current = 100.f;
    float Max     = 100.f;
};
```

No base class, no virtual functions, no ECS-specific annotations. Components are plain
data.

**Step 2 — register before any entity is created:**

```cpp
// In module/zone initialization, before CreateEntity() is called:
world.RegisterComponent<Health>();
```

Registration assigns a stable `ComponentId` and records size/alignment. Registering the
same type twice returns the same id. Registering after the first entity exists is an
assertion failure (v1 restriction).

**Step 3 — add to entities:**

```cpp
EntityId player = world.CreateEntity();
world.AddComponent<LocalTransform>(player, { Transform3f::Identity() });
world.AddComponent<WorldTransform>(player, {});
world.AddComponent<Health>(player, { .Current = 80.f, .Max = 100.f });
```

Or from inside a system, via `CommandBuffer`:

```cpp
cmds.AddComponent<Health>(entity, { .Current = 80.f, .Max = 100.f });
```

---

## Writing a new system

A system is any code that receives a `World&` (or `CommandBuffer&`) and iterates a query.
There is no base class or registration requirement.

**Example: regenerate health each frame for non-frozen entities**

```cpp
#include <ecs/Query.h>
#include <gameplay/Health.h>

void HealthRegenSystem(World& world, float dt)
{
    Query<Write<Health>, Without<TagFrozen>> q(world);
    q.ForEachChunk([dt](auto& view)
    {
        auto health = view.template Write<Health>();
        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            health[i].Current = std::min(
                health[i].Current + 5.f * dt,
                health[i].Max);
        }
    });
}
```

Key points:

- `Query` is constructed once; subsequent calls to `ForEachChunk` reuse the cached
  archetype list. Constructing per frame is valid but allocates (prefer reuse for hot
  paths).
- `view.template Write<Health>()` returns `std::span<Health>` for the current chunk.
  `view.template Read<T>()` returns `std::span<const T>`.
- `view.Count()` is the number of live rows in the chunk (≤ capacity).
- Structural changes inside the callback must go through a `CommandBuffer` — not through
  direct `World` methods.

**Example: mark entities as dead and queue destruction**

```cpp
void DeathSystem(World& world, CommandBuffer& cmds)
{
    Query<Read<Health>> q(world);
    q.ForEachChunk([&cmds](auto& view)
    {
        const auto  health   = view.template Read<Health>();
        const auto* entities = view.Entities();
        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            if (health[i].Current <= 0.f)
                cmds.DestroyEntity(EntityId{ entities[i], 0 });
        }
    });
    // cmds is flushed by the scheduler at the end of this phase.
}
```

> **Note on entity IDs inside ForEachChunk:** `view.Entities()` returns `EntityIndex*`
> (the raw slot index, no generation). To reconstruct a full `EntityId` with generation
> for use outside the query, look up through `World::GetAliveEntities()` or store the
> `EntityId` at creation time. For `cmds.DestroyEntity` the generation is not checked
> at record time — it is validated at flush.

---

## Exit criteria for a new reader

You should now be able to:

1. Define a component struct and register it in module init.
2. Add a component to an entity (directly during init, or via `CommandBuffer` from a
   system).
3. Write a system with a `Query<Read<A>, Write<B>, Without<C>>` and iterate it at chunk
   level with `ForEachChunk`.
4. Use `Changed<T>` to skip unchanged chunks.
5. Queue structural changes through `CommandBuffer` from inside a system.

For lifecycle hooks see `docs/ecs/component-traits.md`.
For the full query accessor reference see `docs/ecs/queries.md`.
For command-buffer semantics see `docs/ecs/command-buffers.md`.
For design decisions and benchmark numbers see `docs/ecs/decisions.md`.
