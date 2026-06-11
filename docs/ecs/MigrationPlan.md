# Sencha ECS: Migration Record

This file used to be the step-by-step plan for replacing the old sparse-set ECS.
That migration is complete. The purpose of this document now is to explain what
landed, what compatibility seams remain, and where future agents should look
before changing ECS behavior.

For day-to-day ECS usage, read `overview.md`, `queries.md`,
`command-buffers.md`, and `component-traits.md` first. Use this document when
you need historical context or a map from the pre-migration design to the code
that exists today.

---

## Current State

Sencha uses a single archetype ECS implementation in `engine/include/ecs` and
`engine/src/ecs`.

The core types are:

- `World`: owns entity lifetime, component metadata, archetypes, resources,
  query-scope guards, lifecycle-hook guards, and structural versioning.
- `EntityId`: generational handle. Storage keeps only `EntityIndex`; generation
  is checked at API boundaries.
- `ComponentId`: per-`World` small integer assigned by
  `World::RegisterComponent<T>()`.
- `ArchetypeSignature`: `std::bitset<256>` of component ids.
- `Archetype`: metadata and chunks for one exact component signature.
- `Chunk`: 16 KB storage block, component columns first and entity indices last.
- `Query<...>`: cached chunk iterator over `Read<T>`, `Write<T>`, `With<T>`,
  `Without<T>`, and `Changed<T>`.
- `CommandBuffer`: records structural mutations and flushes them outside active
  queries.
- `ComponentTraits<T>`: optional `OnAdd` / `OnRemove` lifecycle hooks.

The old sparse-set component stores are no longer the model for component data.
New systems should not introduce per-component stores or parallel ECS storage.

---

## What Replaced The Old ECS

The previous model stored each component type in a separate sparse set and joined
sets at iteration time. The landed model groups entities by exact component
signature. Each archetype owns fixed-size chunks containing column arrays for the
components in that signature.

This has a few important consequences:

- Adding or removing a component is a structural change because the entity moves
  to another archetype.
- Direct structural mutation is legal only when no query and no lifecycle hook is
  active.
- Systems that might mutate structure during iteration must record commands in a
  `CommandBuffer`.
- Query iteration is chunk-first. Hot systems should use `ForEachChunk`; simple
  single-component sweeps may use `World::ForEachComponent<T>()`.
- Tags are empty structs. They occupy a signature bit but no per-row storage.
- Per-frame dirtiness should use `Changed<T>`, not add/remove tags.

---

## Component Registration

Register every component type before creating the first entity in a `World`:

```cpp
world.RegisterComponent<LocalTransform>();
world.RegisterComponent<WorldTransform>();
world.RegisterComponent<Parent>();
world.RegisterComponent<StaticMeshComponent>();
```

`RegisterComponent<T>()` is idempotent for the same `T`, but registration after
entity creation is a debug assertion and release-build undefined behavior. This
keeps component ids, archetype signatures, and column layouts stable for the
lifetime of the world.

The v1 component budget is 256 registered types per world.

---

## Structural Mutation Rules

Use direct `World` methods during setup, loading commits, teardown, and other
places where no query is active:

```cpp
EntityId entity = world.CreateEntity();
world.AddComponent<LocalTransform>(entity, local);
world.AddComponent<WorldTransform>(entity, {});
```

Use `CommandBuffer` from systems that iterate:

```cpp
void MarkDead(World& world, CommandBuffer& cmds)
{
    std::as_const(world).ForEachComponent<Health>(
        [&cmds](EntityId entity, const Health& health)
    {
        if (health.Current <= 0.0f)
            cmds.DestroyEntity(entity);
    });
}
```

`CommandBuffer::Flush()` executes in record order, outside query scope. The flush
path batches contiguous hook-free add/remove runs for the same component type,
but hook-bearing components execute one command at a time to preserve lifecycle
ordering.

---

## Entity Handles In Chunks

`ChunkView::Entities()` returns `EntityIndex` values, not full `EntityId`s:

```cpp
const EntityIndex* indices = view.Entities();
```

Use those indices for side tables keyed by storage slot or for diagnostics. Do
not fabricate `EntityId{index, 0}` in production code. The generation is not in
the chunk, and commands such as `DestroyEntity` validate full handles at flush.

When a system needs valid handles, use `ForEachComponent<T>()`, store an
`EntityId` explicitly in a component, or keep the handle from creation time.

---

## Change Detection

Each chunk stores a last-written frame for every component column. Mutable access
bumps the column version conservatively:

- `Write<T>` bumps once after the chunk callback returns.
- non-const `World::TryGet<T>()` bumps immediately.
- non-const `World::ForEachComponent<T>()` bumps each visited chunk.

Const access does not bump. Use `std::as_const(world)` when a read-only
convenience iterator must not register as a change.

`Changed<T>` filters at chunk granularity. It can report false positives within
the chunk, so downstream systems must not assume every row changed.

---

## Resources And Compatibility Seams

`World` owns resources keyed by type:

```cpp
auto& cache = world.AddResource<MeshCache>(...);
MeshCache* maybe = world.TryGetResource<MeshCache>();
```

Resources are the current home for per-world state such as propagation caches,
asset/cache references, and other singleton data that is not an entity
component.

`World` also still contains a migration-only legacy store bag:

```cpp
world.Register<T>();
world.Ensure<T>();
world.Get<T>();
world.TryGet<T>();
```

Those methods exist to keep older tests and call sites compiling while the rest
of the engine finishes moving to archetype components and resources. They are
not ECS component APIs. Do not use them for new systems.

---

## Transform And Render Systems

Transforms are regular ECS components:

- `LocalTransform`: serialized local transform.
- `WorldTransform`: derived world transform written by propagation.
- `Parent`: optional parent handle.

Propagation lives in `engine/src/world/transform/TransformPropagation.cpp`.
It maintains per-world cache data as a resource and keys pointer caches off
`World::StructuralVersion()`, because entity moves can invalidate chunk/row
addresses without creating new archetypes.

Render extraction consumes ECS data through query-style access and copies
render-critical data into render packets. Render queue items do not hold live
ECS pointers across phases.

---

## Parallelization After The Migration

The job substrate and async lane are now real code:

- `JobSystem` / `ThreadPoolJobSystem` provide frame-lane fork/join work.
- `AsyncTaskQueue` provides cross-frame work with a main-thread commit drain.
- `AsyncZoneLoader` builds detached zone registries off-thread and attaches them
  at the drain point.
- Transform propagation has both serial and zone-parallel entry points.

The default runtime config favors the current target game shape: 2-4 room-sized
zones, one async task thread, serial transform propagation, and an auto-sized
frame pool available for heavier workloads. See `parallelization.md` for the
measured dispatch floor, runtime knobs, and deferred chunk-parallel query design.

---

## Where To Look

- ECS public API: `engine/include/ecs`
- ECS implementation: `engine/src/ecs`
- Core ECS tests: `test/ecs/EcsTests.cpp`
- Command-buffer tests: `test/ecs/EcsTests.cpp`
- Job-system tests: `test/jobs/JobSystemTests.cpp`
- Async task tests: `test/jobs/AsyncTaskQueueTests.cpp`
- Zone load tests: `test/runtime/AsyncZoneLoadTests.cpp`
- Transform propagation: `engine/src/world/transform/TransformPropagation.cpp`
- Frame phase wiring: `engine/src/app/EngineFramePhases.cpp`
- Runtime knobs: `engine/include/core/config/RuntimeConfig.h`

When changing core ECS behavior, update this document only if the migration map
changes. Update the daily-use docs when APIs or contracts change, and update
`decisions.md` when the reason behind a non-obvious choice changes.
