# Sencha ECS: Component Traits

`ComponentTraits<T>` is the opt-in specialization point for per-component lifecycle
behavior. The default specialization is empty — zero overhead for components without hooks.

---

## What ComponentTraits provides

```cpp
// engine/include/ecs/ComponentTraits.h

template <typename T>
struct ComponentTraits
{
    // Default: no hooks.
};

// Concept checks used by World and CommandBuffer to detect hooks:
template <typename T>
concept ComponentHasOnAdd =
    requires(T& component, World& world, EntityId entity)
    { ComponentTraits<T>::OnAdd(component, world, entity); };

template <typename T>
concept ComponentHasOnRemove =
    requires(const T& component, World& world, EntityId entity)
    { ComponentTraits<T>::OnRemove(component, world, entity); };
```

Hook presence is detected via C++20 concepts — you do not need to set boolean flags.
Define `OnAdd` if the component has add behavior; define `OnRemove` if it has remove
behavior. You can define one without the other.

---

## Adding hooks

Specialize `ComponentTraits<T>` in a header **near the component type definition**:

```cpp
// engine/include/render/StaticMeshComponent.h

template <>
struct ComponentTraits<StaticMeshComponent>
{
    static void OnAdd(StaticMeshComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<StaticMeshComponentAssets>();
        if (assets == nullptr) return;
        if (assets->Meshes)     assets->Meshes->Retain(component.Mesh);
        if (assets->Materials)  assets->Materials->Retain(component.Material);
    }

    static void OnRemove(const StaticMeshComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<StaticMeshComponentAssets>();
        if (assets == nullptr) return;
        if (assets->Materials)  assets->Materials->Release(component.Material);
        if (assets->Meshes)     assets->Meshes->Release(component.Mesh);
    }
};
```

Hook signatures:

```cpp
// OnAdd receives a mutable reference to the just-added component.
static void OnAdd(T& component, World& world, EntityId entity);

// OnRemove receives a const reference — the component is about to be removed.
static void OnRemove(const T& component, World& world, EntityId entity);
```

---

## When hooks fire

| Operation                               | Hook fired         | Timing                          |
|-----------------------------------------|--------------------|----------------------------------|
| `world.AddComponent<T>(entity, value)`  | `OnAdd`            | Immediately, inline              |
| `world.RemoveComponent<T>(entity)`      | `OnRemove`         | Before the entity moves archetype|
| `cmds.AddComponent<T>(entity, value)`   | `OnAdd`            | During `CommandBuffer::Flush`    |
| `cmds.RemoveComponent<T>(entity)`       | `OnRemove`         | During `CommandBuffer::Flush`    |

Hooks run **synchronously** at the call site (for direct mutations) or during flush (for
command buffer operations). They execute in the order commands were recorded for
command-buffer flushes.

---

## What hooks may do

Hooks may:

- Read and write resources via `world.TryGetResource<T>()` / `world.GetResource<T>()`.
- Retain or release external assets (ref-count handles, GPU resource lifetimes).
- Emit log messages.
- Read and mutate already-present components on the same entity via
  `world.TryGet<OtherComponent>(entity)` — **but only components that the hook did not
  add or remove**, because the entity's archetype is in mid-transition.

Hooks must **not**:

- Call `world.AddComponent`, `world.RemoveComponent`, `world.CreateEntity`, or
  `world.DestroyEntity` — directly or via a command buffer. This is enforced by
  `World::LifecycleHookDepth` and will assertion-fail in debug builds.
- Perform cascading structural mutations. If cascading behavior is needed, the system
  that originated the command records the cascade *before* the flush, not inside the hook.

---

## Batching and hooks

`CommandBuffer::Flush` detects contiguous runs of `AddComponent<T>` or
`RemoveComponent<T>` commands for the same `T` **where T has no lifecycle hook** and
executes them as a batch (one bulk archetype move instead of N individual moves). If T
has a hook, the batch optimization does not apply — each command executes individually in
record order to preserve hook semantics.

If you add a hook to a component that is frequently added/removed in bulk, measure the
performance impact. See `docs/ecs/decisions.md` D1.5 for the batching strategy.

---

## Discovering all hooked components

```sh
grep -r "ComponentTraits<" engine/include/ engine/src/
```

By design, `ComponentTraits` specializations live near the component definition.
`grep ComponentTraits` in the engine tree lists every component with hooks.

---

## When to use hooks vs. when not to

**Use hooks for:**

- External resource retain/release tied to component lifetime (`StaticMeshComponent`
  mesh handle, `AudioComponent` clip handle).
- Logging that a component was added or removed for debugging.
- Registering an entity with an external service that must know the exact add/remove order.

**Do not use hooks for:**

- Structural mutations (adding another component, destroying the entity). Record those
  commands explicitly in the system before flush.
- Cross-entity side effects that depend on multiple entities' component state — that is
  a system, not a hook.
- High-frequency per-frame state. Hooks run at structural-change time, not per-frame.

The rule from `docs/ecs/MigrationPlan.md` (A6, P6): hooks are synchronous, traceable,
and minimal. They are not an observer bus.
