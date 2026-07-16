# Sencha ECS: Component Traits

`ComponentTraits<T>` is the opt-in specialization point for component lifetime edges. The default specialization is empty, so components without hooks pay no hook-dispatch cost.

## Contract

A hook receives:

- the component being added or removed
- the owning registry's `ResourceStore`
- the entity id associated with the component

It does not receive `EntityStore`, `Registry`, `Engine`, or `ZoneRuntime`.

```cpp
// engine/include/ecs/ComponentTraits.h

template <typename T>
struct ComponentTraits
{
};

template <typename T>
concept ComponentHasOnAdd =
    requires(T& component, ResourceStore& resources, EntityId entity)
    {
        ComponentTraits<T>::OnAdd(component, resources, entity);
    };

template <typename T>
concept ComponentHasOnRemove =
    requires(const T& component, ResourceStore& resources, EntityId entity)
    {
        ComponentTraits<T>::OnRemove(component, resources, entity);
    };
```

Hook presence is detected through C++20 concepts. No boolean marker is required. A component may define either hook or both.

Hooks dispatch only for non-empty component types. Empty tag components have no stored object pointer and must not rely on lifecycle hooks.

## Defining hooks

Specialize `ComponentTraits<T>` beside the component definition:

```cpp
#include <core/ResourceStore.h>

template <>
struct ComponentTraits<StaticMeshComponent>
{
    static void OnAdd(
        StaticMeshComponent& component,
        ResourceStore& resources,
        EntityId)
    {
        auto* assets = resources.TryGet<StaticMeshComponentAssets>();
        if (assets == nullptr)
            return;

        if (assets->Meshes != nullptr)
            assets->Meshes->Retain(component.Mesh);
        if (assets->MaterialSets != nullptr)
            assets->MaterialSets->Retain(component.Materials);
    }

    static void OnRemove(
        const StaticMeshComponent& component,
        ResourceStore& resources,
        EntityId)
    {
        auto* assets = resources.TryGet<StaticMeshComponentAssets>();
        if (assets == nullptr)
            return;

        if (assets->MaterialSets != nullptr)
            assets->MaterialSets->Release(component.Materials);
        if (assets->Meshes != nullptr)
            assets->Meshes->Release(component.Mesh);
    }
};
```

`OnAdd` receives a mutable component because initialization may update runtime fields. `OnRemove` receives a const component because teardown consumes the existing lifetime state without rewriting it.

## Resource binding

Every runtime `Registry` owns one `ResourceStore` and constructs its `EntityStore` with that store. Member order keeps the store alive while `EntityStore` destruction dispatches removal hooks.

A standalone `EntityStore` may be default-constructed only when it registers components without hooks. Registering a hook-bearing component requires construction with a stable resource store:

```cpp
ResourceStore resources;
EntityStore world(resources);
world.RegisterComponent<MyHookedComponent>();
```

The resource store must outlive the world.

A hook may tolerate an absent resource type when that is a valid runtime posture. Headless registries, tests, and editor documents may omit bindings such as audio or rendering caches. The registry still supplies its resource store, and the hook uses `TryGet<T>()` to detect the optional binding.

## When hooks fire

| Operation | Hook | Timing |
| --- | --- | --- |
| `world.AddComponent<T>` | `OnAdd` | Immediately after the destination row is initialized |
| `world.RemoveComponent<T>` | `OnRemove` | Before the source row is removed |
| `world.DestroyEntity` | Every present `OnRemove` | Before the entity row and id are destroyed |
| `world.ClearEntities` | Every present `OnRemove` | Once per live component before storage is cleared |
| buffered add | `OnAdd` | During `CommandBuffer::Flush` |
| buffered remove | `OnRemove` | During `CommandBuffer::Flush` |
| buffered destroy | Every present `OnRemove` | During `CommandBuffer::Flush` |
| world or registry destruction | Every present `OnRemove` | Before the owning resource store is destroyed |

Hooks execute synchronously. Buffered hooks execute in command order. Entity destruction dispatches component removal hooks in component registration order.

The engine clears loaded registry entities before game shutdown and before module unregistration. This keeps game-owned assets, engine audio and caption services, and module-owned hook code alive during teardown.

## Allowed work

Hooks may:

- read and mutate the component passed to `OnAdd`
- read registry-scoped bindings through `resources.Get<T>()` or `resources.TryGet<T>()`
- retain or release external assets
- stop external playback or end external captions
- update non-ECS state owned by a registry resource
- emit diagnostic logging through an explicitly registered binding

Hooks must not:

- add or remove components
- create or destroy entities
- inspect or mutate unrelated components
- enqueue hidden structural changes
- perform cross-entity gameplay behavior
- throw exceptions

The API enforces the structural rule by not providing entity storage. A lifecycle hook that needs other component data is the wrong mechanism. Move that behavior into a system and record structural changes through `CommandBuffer`.

## Batching

`CommandBuffer::Flush` batches contiguous add or remove operations only when the component has no lifecycle hook. Hook-bearing components execute individually so hook order and exact-once behavior remain visible.

Adding hooks to a component can therefore remove it from structural batch paths. Measure components that are added or removed in large volumes.

## Appropriate uses

Use hooks for external lifetime edges that must exactly follow component lifetime:

- retaining and releasing mesh, material-set, or audio handles
- stopping a voice before releasing its audio clip
- ending an active caption during component or zone teardown
- unregistering a component-owned handle from a registry-local service

Do not use hooks for gameplay reactions, cascading entity changes, per-frame updates, or observer-style event distribution. Those belong in systems.

The rule is deliberately narrow: component hooks maintain lifetime edges. They are not an event bus.
