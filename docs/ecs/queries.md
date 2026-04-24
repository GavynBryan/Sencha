# Sencha ECS: Query Cookbook

A query declares what components a system reads or writes. Every accessor combination
is listed here with a short example and the rules that govern it.

---

## Accessor reference

```cpp
Read<T>     // const access. Archetype must have T.
Write<T>    // mutable access. Archetype must have T. Bumps column version.
With<T>     // archetype must have T; no span is yielded. Used for tags.
Without<T>  // archetype must NOT have T.
Changed<T>  // chunk-level filter: skip chunks whose T column was not written
            // since the reference frame. Archetype must have T.
```

Accessors are combined as template parameters to `Query<...>`:

```cpp
Query<Read<A>, Write<B>, Without<C>, Changed<D>, With<TagE>> q(world);
```

---

## Constructing and reusing queries

```cpp
// Construct once — resolves ComponentIds and builds the matching-archetype list.
Query<Read<Health>> q(world);

// Iterate — uses cached archetype list. Zero hash-map probes in the inner loop.
q.ForEachChunk([](auto& view) { /* ... */ });

// The list rebuilds lazily if new archetypes have been created since last use.
// Constructing a new Query each call is correct but incurs one allocation per call.
// For hot paths, keep the Query alive across frames.
```

---

## ForEachChunk

```cpp
q.ForEachChunk(callback);
// or with an explicit reference frame for Changed<T>:
q.ForEachChunk(callback, referenceFrame);
```

- `callback` receives `ChunkView<Accessors...>&`.
- `referenceFrame` defaults to 0, which matches any chunk that has ever been written.
  Pass `world.CurrentFrame() - 1` to match only chunks written since the previous frame.

---

## Read\<T\>

```cpp
Query<Read<Position>, Read<Velocity>> q(world);
q.ForEachChunk([](auto& view)
{
    const auto pos = view.template Read<Position>(); // std::span<const Position>
    const auto vel = view.template Read<Velocity>(); // std::span<const Velocity>
    for (uint32_t i = 0; i < view.Count(); ++i)
        // read pos[i] and vel[i]
});
```

`Read<T>` does not bump the T column version. Multiple queries with `Read<T>` on the
same chunk in the same frame see the same version counter.

---

## Write\<T\>

```cpp
Query<Read<Position>, Write<Velocity>> q(world);
q.ForEachChunk([dt](auto& view)
{
    const auto pos = view.template Read<Position>();
    auto       vel = view.template Write<Velocity>(); // std::span<Velocity>
    for (uint32_t i = 0; i < view.Count(); ++i)
        vel[i].X += pos[i].X * dt;
});
```

`Write<T>` grants mutable access and **bumps the T column version once per chunk** after
the callback returns — whether or not any row was actually written. This is conservative:
a system that holds `Write<T>` but never modifies any row will still cause `Changed<T>`
to match that chunk this frame.

---

## With\<T\> — filter by presence, no data access

```cpp
// Only entities that have both Health and the TagAlive tag are visited.
Query<Write<Health>, With<TagAlive>> q(world);
q.ForEachChunk([](auto& view)
{
    auto health = view.template Write<Health>();
    for (uint32_t i = 0; i < view.Count(); ++i)
        health[i].Current -= 1.f;
});
```

`With<T>` does not yield a span. It is the correct accessor for tag components (since
tag columns carry no per-entity data) and for expressing that a component must be present
without needing to read it.

---

## Without\<T\> — exclude archetypes

```cpp
// Apply damage only to entities that are NOT frozen.
Query<Write<Health>, Without<TagFrozen>> q(world);
q.ForEachChunk([dmg](auto& view)
{
    auto health = view.template Write<Health>();
    for (uint32_t i = 0; i < view.Count(); ++i)
        health[i].Current -= dmg;
});
```

Any archetype that has the `TagFrozen` bit set in its signature is excluded from
`MatchingArchetypes`. No per-entity check is needed inside the callback.

---

## Changed\<T\> — skip unchanged chunks

```cpp
// Only process entities whose LocalTransform was written since the previous frame.
Query<Read<LocalTransform>, Write<WorldTransform>, Changed<LocalTransform>> q(world);

const uint32_t prevFrame = world.CurrentFrame() - 1;
q.ForEachChunk([](auto& view)
{
    const auto local = view.template Read<LocalTransform>();
    auto       world = view.template Write<WorldTransform>();
    for (uint32_t i = 0; i < view.Count(); ++i)
        world[i].Value = local[i].Value; // simplified: no parent
}, prevFrame);
```

`Changed<T>` requires that `T` also be present in the archetype (it contributes to
`RequiredSig`). It does not yield a span — it is a filter only.

**Reference frame semantics:**

| `referenceFrame` | Matches chunk if...                              |
|-----------------|--------------------------------------------------|
| `0`             | T column was ever written (LastWrittenFrame > 0) |
| `N`             | T column was written after frame N               |
| `CurrentFrame() - 1` | T column was written in the previous frame |

**Conservative semantics**: a chunk that had `Write<T>` access granted but no rows
actually modified will still pass the filter. Systems using `Changed<T>` must handle
chunks that contain a mix of changed and unchanged rows.

---

## Combining multiple accessors

```cpp
// Full example: propagate transforms for parented entities whose
// LocalTransform changed since last frame.
Query<Read<LocalTransform>, Write<WorldTransform>,
      With<Parent>, Changed<LocalTransform>> q(world);

q.ForEachChunk([](auto& view)
{
    const auto local = view.template Read<LocalTransform>();
    auto       wt    = view.template Write<WorldTransform>();
    // Parent is required (With<Parent>) but not read here.
    for (uint32_t i = 0; i < view.Count(); ++i)
        wt[i].Value = local[i].Value; // caller handles parent multiplication
}, world.CurrentFrame() - 1);
```

---

## Entity-level iteration (convenience)

`ForEachChunk` is the primary entry point and the one used in hot paths. For one-off
queries where ergonomics matter more than performance:

```cpp
world.ForEachComponent<Health>([](EntityId entity, Health& h)
{
    h.Current = h.Max; // full heal
});
```

`ForEachComponent<T>` iterates all entities with T across all archetypes, calling the
callback per entity. It is not a `Query` — it cannot filter by additional components or
by change detection. Use it for simple single-component sweeps during init or debug code.

---

## Accessing entities inside ForEachChunk

```cpp
q.ForEachChunk([&cmds](auto& view)
{
    const auto  health   = view.template Read<Health>();
    const EntityIndex* indices = view.Entities(); // EntityIndex* (raw slot index)

    for (uint32_t i = 0; i < view.Count(); ++i)
    {
        if (health[i].Current <= 0.f)
        {
            // EntityId needs the generation, which is not available from the chunk.
            // Use world.TryGet or the EntityId stored elsewhere.
            // For destruction via CommandBuffer, pass the index as EntityId with
            // generation=0 — DestroyEntity validates at flush, not at record time.
            cmds.DestroyEntity(EntityId{ indices[i], 0 });
        }
    }
});
```

---

## Query scope guard

`ForEachChunk` pushes a query-scope guard on entry and pops it on exit. While the guard
is active, any direct call to `World::AddComponent`, `World::RemoveComponent`,
`World::CreateEntity`, or `World::DestroyEntity` is an assertion failure in debug builds.
Use `CommandBuffer` instead.

---

## Performance notes

- Construct the `Query` once (at system init or as a field) to avoid the one-time
  archetype-list build cost per call.
- Column indices are cached per archetype (not per chunk), so there is no per-chunk
  linear scan in the hot path.
- `ComponentId`s are resolved once at `Query` construction, so there are no hash-map
  probes inside `ForEachChunk`.
- Empty chunks are skipped by `ForEachChunk` (`chunk.IsEmpty()` check). After many
  removals, empty chunks accumulate but do not affect correctness or throughput.
