# Sencha ECS: Component Traits

`ComponentTraits<T>` is the opt-in specialization point for per-component lifecycle
behavior. The default specialization is empty, so components without hooks pay no
hook-dispatch cost.

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

Hook presence is detected via C++20 concepts. You do not need to set boolean flags.
Define `OnAdd` if the component has add behavior; define `OnRemove` if it has remove
behavior. You can define one without the other.

Current implementation note: hooks dispatch only for non-empty component types.
Empty tag components have no stored object pointer, so they should not rely on
`OnAdd` or `OnRemove`.

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

// OnRemove receives a const reference. The component still exists and the
// entity is still alive while the hook runs.
static void OnRemove(const T& component, World& world, EntityId entity);
```

---

## When hooks fire

| Operation | Hook fired | Timing |
|-----------|------------|--------|
| `world.AddComponent<T>(entity, value)` | `OnAdd` for non-empty T | Immediately, inline |
| `world.RemoveComponent<T>(entity)` | `OnRemove` for non-empty T | Before the entity moves archetype |
| `world.DestroyEntity(entity)` | Every present `OnRemove` | Before the entity row is removed |
| `world.ClearEntities()` | Every present `OnRemove` | Once per live entity before storage clears |
| `cmds.AddComponent<T>(entity, value)` | `OnAdd` for non-empty T | During `CommandBuffer::Flush` |
| `cmds.RemoveComponent<T>(entity)` | `OnRemove` for non-empty T | During `CommandBuffer::Flush` |
| `cmds.DestroyEntity(entity)` | Every present `OnRemove` | During `CommandBuffer::Flush` |
| `Registry` or `World` destruction | Every present `OnRemove` | Before owned resources are destroyed |

Hooks run synchronously at the call site, during command-buffer flush, or during
explicit teardown. Command-buffer hooks execute in recorded command order.
Destruction dispatches component removal hooks in component registration order.

`Engine::Run` clears all loaded registry entities before calling `Game::OnShutdown`.
This keeps game-owned assets, engine audio/caption services, and module-owned hook
code alive until component teardown is complete.

---

## What hooks may do

Hooks may:

- Read and write resources via `world.TryGetResource<T>()` / `world.GetResource<T>()`.
- Retain or release external assets such as ref-counted handles and GPU resources.
- Emit log messages.
- Read and mutate already-present components on the same entity via
  `world.TryGet<OtherComponent>(entity)`, but only components that the hook did not
  add or remove because the entity's archetype is in mid-transition.

Hooks must **not**:

- Call `world.AddComponent`, `world.RemoveComponent`, `world.CreateEntity`,
  `world.DestroyEntity`, or `world.ClearEntities`, directly or through a command
  buffer. `World::LifecycleHookDepth` enforces this in debug builds.
- Perform cascading structural mutations. The originating system records cascades
  before flush instead of hiding them inside a hook.
- Throw exceptions. Removal hooks run from entity, registry, and world teardown;
  throwing from a destructor path terminates the process.

---

## Batching and hooks

`CommandBuffer::Flush` detects contiguous runs of `AddComponent<T>` or
`RemoveComponent<T>` commands for the same `T` where T has no lifecycle hook and
executes them as a batch. Hook-bearing components execute individually in record
order to preserve lifecycle semantics.

If you add a hook to a component that is frequently added or removed in bulk,
measure the performance impact. See `docs/ecs/decisions.md` D1.5 for the batching
strategy.

---

## Discovering all hooked components

```sh
rg "ComponentTraits<" engine/include engine/src
```

By design, `ComponentTraits` specializations live near the component definition.
`rg ComponentTraits` in the engine tree lists every component with hooks.

---

## When to use hooks vs. when not to

**Use hooks for:**

- External resource retain/release tied to component lifetime, such as mesh or
  audio clip handles.
- Logging that a component was added or removed for debugging.
- Registering an entity with an external service that needs exact add/remove order.

**Do not use hooks for:**

- Structural mutations such as adding another component or destroying the entity.
  Record those commands explicitly in the system before flush.
- Cross-entity side effects that depend on multiple entities' component state.
  That is a system, not a hook.
- High-frequency per-frame state. Hooks run at structural-change time, not per frame.

The rule is intentionally small: hooks are synchronous, traceable, and minimal.
They are not an observer bus.
