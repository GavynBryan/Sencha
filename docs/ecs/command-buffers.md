# Sencha ECS: Command Buffers

A `CommandBuffer` records structural mutations — adding/removing components, creating and
destroying entities — during system execution. Commands are applied to the `World` at
flush time, which happens at explicit scheduler phase boundaries, outside any active query.

---

## Why command buffers exist

Structural changes move entities between archetypes. Moving an entity invalidates the
chunk pointer and row index for that entity — and potentially for the entity that was
swap-and-popped into its vacated slot. If that move happened mid-query, the query's spans
would be invalidated.

The invariant: **no structural change may happen while a `ForEachChunk` callback is
executing.** Command buffers enforce this by deferring every structural change to flush
time. The `World` maintains a query-depth counter; any direct structural call while the
counter is non-zero is an assertion failure in debug builds.

---

## Creating a CommandBuffer

```cpp
CommandBuffer cmds(world);
```

`CommandBuffer` does not own the `World`. The world must outlive the buffer.

---

## Recording commands

All recording methods return `void`. Commands are not executed immediately.

### AddComponent\<T\>

```cpp
cmds.AddComponent<Health>(entity, { .Current = 80.f, .Max = 100.f });
cmds.AddComponent<TagFrozen>(entity); // tag: no value needed (default T{})
```

The component value is copied into the buffer's payload arena at record time. The arena
is a `std::vector<uint8_t>` that grows on demand; there are no per-command heap
allocations.

### RemoveComponent\<T\>

```cpp
cmds.RemoveComponent<Health>(entity);
cmds.RemoveComponent<TagFrozen>(entity);
```

### DestroyEntity

```cpp
cmds.DestroyEntity(entity);
```

If the entity is no longer alive at flush time, the command is a no-op.

### CreateEntity

```cpp
cmds.CreateEntity(); // creates an entity in the empty archetype at flush
```

To create with initial components, record `CreateEntity()` followed by `AddComponent`
commands for the same logical entity. Note: the new entity's `EntityId` is not available
at record time — it is assigned during flush. If you need the id after flush, use
`World::CreateEntity()` directly (only legal outside query scope and lifecycle hooks).

---

## Flushing

```cpp
cmds.Flush();
```

Executes all recorded commands against the `World` in record order. Flush must be called
outside any active `ForEachChunk` callback. The scheduler calls flush at explicit phase
boundaries; manual flush is also legal during init/teardown.

`Flush` clears the command list and payload arena on completion. A flushed buffer can be
reused immediately.

```cpp
bool   empty = cmds.IsEmpty(); // true if no commands are pending
size_t count = cmds.Count();   // number of pending commands
cmds.Clear();                  // discard all pending commands without executing
```

---

## Flush semantics: record order

Commands execute in **the order they were recorded**. If you record:

```cpp
cmds.AddComponent<Health>(e, { 50.f, 100.f });
cmds.RemoveComponent<TagFrozen>(e);
```

Then at flush, Health is added first, then TagFrozen is removed. The entity passes
through the `{... + Health}` archetype before transitioning to `{... + Health - TagFrozen}`.

---

## Batch optimization

`CommandBuffer::Flush` detects contiguous runs of `AddComponent<T>` or
`RemoveComponent<T>` for the same `T` **where T has no lifecycle hook** and executes
them as a single bulk archetype move:

```cpp
// These three commands form a contiguous hook-free AddComponent<TagDead> run:
cmds.AddComponent<TagDead>(e0);
cmds.AddComponent<TagDead>(e1);
cmds.AddComponent<TagDead>(e2);
// → one bulk move (all three source archetypes resolved, then moved at once)
```

If the run is interrupted by a different command kind, a different component type, or a
component with a lifecycle hook, the batch ends and subsequent commands execute
individually. This optimization covers the common "add the same tag to N entities in a
single system pass" workload.

---

## Structural-change invariants

The following conditions are assertion failures in debug builds:

| Invariant                                         | Mechanism                        |
|---------------------------------------------------|----------------------------------|
| Direct structural mutation inside `ForEachChunk`  | `World::QueryDepth` guard        |
| Direct structural mutation inside a lifecycle hook| `World::LifecycleHookDepth` guard|
| Lifecycle hook calls structural mutation          | Same depth guard, checked on entry|
| `Flush` called while a query is active            | `World::InQueryScope()` assertion |

In release builds these are documented as undefined behavior. The guards are compile-time
`assert()`s that strip in NDEBUG builds.

---

## Nested commands and ordering

There is no concept of nested command buffers. A system creates one `CommandBuffer`,
records commands throughout its execution, and the scheduler flushes it once per phase.

Lifecycle hooks run during flush. If an `OnAdd` hook on component A would logically
require adding component B to the same entity, the correct approach is: the system that
adds A also records an `AddComponent<B>` command in the same buffer, before flush.
The hook must not call `AddComponent<B>` itself — that would assertion-fail.

---

## Example: a full system with command buffer

```cpp
void PruneDeadEntities(World& world, CommandBuffer& cmds)
{
    // Read Health across all entities; queue destruction for those at zero HP.
    Query<Read<Health>> q(world);
    q.ForEachChunk([&cmds](auto& view)
    {
        const auto health  = view.template Read<Health>();
        const auto indices = view.Entities();

        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            if (health[i].Current <= 0.f)
            {
                // generation=0: DestroyEntity validates alive-ness at flush.
                cmds.DestroyEntity(EntityId{ indices[i], 0 });
            }
        }
    });
    // cmds.Flush() is called by the scheduler at the end of this phase.
    // Do NOT call cmds.Flush() inside ForEachChunk.
}
```
