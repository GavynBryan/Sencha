# Runtime ECS Completion Plan

Status: phases 0 through 7 landed. `ResourceStore`, immobile registries with
member-order teardown, resource-only component lifecycle hooks, registry-local
runtime state (propagation cache, physics scene, mover pool, activation queue)
on `Registry::Resources`, session definitions centralized in the global
registry, deletion of the entity-store resource map, and the public rename of
`World` to `EntityStore` (with `Registry::Components` now `Registry::Entities`)
are all in. Remaining: the phase 8 through 9 internal decomposition and API
pass.

This plan is runtime-only. Kyusu editor decomposition remains on the separate `agent/editor-decomposition-plan` branch.

## Objective

Complete `agent/runtime-world-stabilization` with an ECS architecture that has:

- one owner for registry-local resources
- one mechanically named entity storage facade
- explicit session-wide definition ownership
- deterministic construction and teardown
- no compatibility aliases
- no forwarding facade around the actual ECS
- no public exposure of archetype internals
- no speculative interface hierarchy

The target runtime shape is:

```cpp
struct Registry
{
    RegistryId Id;
    RegistryKind Kind;
    ZoneId Zone;

    ResourceStore Resources;
    EntityStore Entities;
};
```

`Registry` is the runtime unit owned by `ZoneRuntime`. `ResourceStore` owns non-component state. `EntityStore` owns entity identity, component registration, archetypes, queries, structural mutation, epochs, and lifecycle dispatch.

## Landed invariants

- The runtime owns ECS epoch advancement.
- Structural additions stamp their destination component columns.
- Query and component iteration guards restore correctly when callbacks throw.
- Partial writes are published when mutable iteration throws.
- Component metadata is the authority for type-erased layout and lifecycle dispatch.
- Removal hooks run for direct and buffered removal, entity destruction, registry clear, zone destruction, and engine shutdown.
- Registry entities are cleared while registry resources, engine services, and game-owned assets are alive.
- The legacy store bag and `RegistryEntityFacade` are removed.

## Non-goals

This branch does not add:

- per-entity change tracking
- chunk-parallel queries
- non-trivially-copyable components
- a new scripting boundary
- dependency injection infrastructure
- a global service locator
- gameplay features
- a second ECS API
- compatibility aliases for renamed types

Unrelated forwarding APIs are outside this branch unless the ECS migration proves them dead.

## Fixed architectural decisions

### `World` becomes `EntityStore`

The current type stores and coordinates entities, components, archetypes, queries, structural versions, and change epochs. It does not represent the game world.

Final names:

- `World` becomes `EntityStore`
- `Registry::Components` becomes `Registry::Entities`
- the private `EntityRegistry Entities` member receives a distinct mechanical name
- `World.h` becomes `EntityStore.h`

No redirector header, type alias, deprecated member, or forwarding facade remains.

### `ResourceRegistry` becomes `ResourceStore`

`ResourceRegistry` is a concrete type-indexed owner. Move it to a neutral lower layer such as `engine/include/core/ResourceStore.h`.

Required behavior:

- `Register<T>`
- `Ensure<T>`
- `Get<T>`
- `TryGet<T>`
- `Has<T>`
- explicit `Clear`
- duplicate registration rejection
- deterministic reverse-registration destruction
- move-only unless registry immobility removes the need for movement

It has no parent lookup, fallback chain, singleton, engine pointer, or automatic service discovery.

### `Registry` is non-copyable and non-movable

Registries are heap-owned by `ZoneRuntime` and transferred through `std::unique_ptr`. Moving a registry complicates its entity store's non-owning resource binding and creates avoidable teardown cases.

Replace factory-return-by-value construction with direct constructors:

```cpp
Registry(RegistryId id, RegistryKind kind, ZoneId zone);
```

Delete copy construction, copy assignment, move construction, and move assignment.

### Resources are declared before entities

The final member order is:

```cpp
ResourceStore Resources;
EntityStore Entities;
```

`EntityStore` receives a non-owning resource reference during construction.

Reverse member destruction then guarantees:

1. `EntityStore` destroys entities and dispatches removal hooks.
2. `ResourceStore` remains alive for every hook.
3. Resources are destroyed only after all components are gone.

Once member order enforces this invariant, remove the custom `Registry` destructor that manually clears entities.

### Lifecycle hooks receive resources, not the ECS

Final trait shape:

```cpp
template <>
struct ComponentTraits<MyComponent>
{
    static void OnAdd(
        MyComponent& component,
        ResourceStore& resources,
        EntityId entity);

    static void OnRemove(
        const MyComponent& component,
        ResourceStore& resources,
        EntityId entity);
};
```

`ComponentMeta` stores type-erased functions with the same dependency boundary.

Hooks do not receive `EntityStore`, `Registry`, `Engine`, or `ZoneRuntime`. They cannot perform structural mutation because they are not given entity storage.

The resource store is explicit and registry-scoped. It must not become a global access path or a convenience parameter passed through unrelated algorithms.

### Runtime algorithms receive exact dependencies

Functions that need only component data accept `EntityStore&`.

Functions that need entity data and registry-local state accept `Registry&`, then resolve the exact resource once before entering hot loops.

Functions that need session definitions accept explicit definition references. Schedule adapters resolve those references from the global registry.

Do not make every system accept `ResourceStore&`. Core algorithms receive the specific values they consume.

### Session definitions exist once

The following are session-wide definitions and live in the global registry resource store:

- `GameplayTagRegistry`
- `AttributeRegistry`
- `EffectRegistry`
- `AbilityRegistry`
- `MovementTags`
- `MovementDefs`
- `LocomotionModeRegistry`

They are registered once during global runtime initialization. A streamed zone must not create its own copies.

The following remain registry-local:

- `ActiveCameraService`
- `StaticMeshComponentAssets`
- `AudioSourceRuntime`
- `PropagationOrderCache`
- `PhysicsScene`
- `CharacterMoverPool`
- `AbilityActivationQueue`

The following remain owned by scheduled systems or engine services:

- `PhysicsWorld`
- `CollisionShapeCache`
- renderer caches
- audio service
- asset system
- logging provider

Registry-local binding resources may hold non-owning pointers to those external owners.

### Serialization receives definitions explicitly

Serializers must not recover session definitions from entity storage.

Extend `SceneSerializationContext` only with definition registries consumed by real codecs, initially gameplay tags and attributes. Detached asynchronous zone construction captures the same session definition pointers. It does not duplicate definitions into a detached registry.

## Execution sequence

Every phase ends with a green CI run. Do not begin the next phase on a failing head.

## Phase 0: Baseline and exhaustive inventory

1. Restore green CI on the corrected compatibility-removal head.
2. Search the repository for:
   - `AddResource`
   - `GetResource`
   - `TryGetResource`
   - `HasResource`
   - `registry.Resources`
   - `ComponentTraits<`
   - `Registry::Components`
   - `<ecs/World.h>`
3. Record every resource type, registration site, consumer, final owner, construction timing, and destruction dependency.
4. Classify each resource as:
   - session definition
   - registry-local state
   - scheduled-system-owned state
   - engine-owned or game-owned external dependency
5. Add missing tests for ownership paths that later phases will change.

Stop condition:

- every current resource has a declared destination
- baseline CI is green
- no resource is classified by name alone

## Phase 1: Establish `ResourceStore`

1. Rename and relocate `ResourceRegistry` to `ResourceStore`.
2. Use type-indexed lookup plus registration-order ownership.
3. Destroy resources in deterministic reverse registration order.
4. Add tests for registration, duplicate rejection, ensure semantics, lookup, reverse destruction, and explicit clear.
5. Migrate `Registry::Resources` to `ResourceStore`.
6. Leave the old entity-store resource map untouched in this phase.

Stop condition:

- `ResourceStore` is independently tested
- `Registry::Resources` uses it
- no resource has changed owner yet
- CI is green

## Phase 2: Make registry lifetime mechanically safe

1. Add direct registry constructors.
2. Declare resources before entity storage.
3. Construct entity storage with a resource reference.
4. Delete registry copy and move operations.
5. Remove value-returning registry factories if direct construction replaces their purpose.
6. Update `ZoneRuntime`, asynchronous loading, tests, examples, and template code.
7. Add compile-time assertions proving registry immobility.
8. Add teardown tests proving resources remain alive while entities are destroyed.

Stop condition:

- registry values never move
- ownership is expressed through `std::unique_ptr`
- member order enforces teardown
- CI is green

## Phase 3: Narrow component lifecycle hooks

1. Change `ComponentTraits` concepts from `World&` to `ResourceStore&`.
2. Change `ComponentMeta::OnAdd` and `OnRemove` signatures.
3. Store the resource reference in entity storage as a non-owning lifecycle dependency.
4. Update typed, raw, buffered, destruction, clear, and destructor lifecycle paths.
5. Migrate production hooks:
   - `StaticMeshComponent`
   - `AudioSourceComponent`
   - `AudioCaptionComponent`
6. Migrate tests and game-module hook examples.
7. Delete lifecycle access to entity storage.
8. Update lifecycle documentation.

Tests must prove:

- hooks receive the correct registry resources
- direct and buffered operations use the same resources
- teardown sees resources alive
- hooks run exactly once
- registries cannot observe each other's resources
- game-defined hooks work across the module boundary

Stop condition:

- no lifecycle callback accepts `World`, `EntityStore`, or `Registry`
- lifecycle behavior is resource-only
- CI is green

## Phase 4: Move registry-local runtime state

Move one subsystem at a time.

### Render and audio bindings

Move `StaticMeshComponentAssets` and `AudioSourceRuntime` to `Registry::Resources`. Register them before scene deserialization adds hook-bearing components.

### Transform propagation

Move `PropagationOrderCache` to registry resources. Refactor propagation so the registry adapter obtains the cache once and calls an algorithm that receives `EntityStore&` and `PropagationOrderCache&`.

Do not perform resource lookup inside the propagation sweep.

### Physics

Move `PhysicsScene` and `CharacterMoverPool` to registry resources. `PhysicsWorld` and shape caches remain system-owned.

### Ability activation runtime state

Move `AbilityActivationQueue` to each registry's resource store. The queue remains isolated per registry. Definitions do not.

Tests must prove resource isolation across registries, correct detached-zone behavior, and correct lifecycle binding selection.

Stop condition:

- mutable registry-local state is owned by `Registry::Resources`
- the old entity-store resource map contains only session definitions, if anything
- CI is green

## Phase 5: Centralize session definitions

Split component storage registration, definition registration, and registry-runtime registration.

Target surfaces:

```cpp
void RegisterAbilityComponents(EntityStore& entities);
void RegisterAbilityDefinitions(ResourceStore& sessionResources);
void RegisterAbilityRuntime(ResourceStore& registryResources);

void RegisterMovementComponents(EntityStore& entities);
void RegisterMovementDefinitions(ResourceStore& sessionResources);
```

Implementation:

1. Register gameplay tag, attribute, effect, ability, movement, and locomotion definitions once in the global registry.
2. Register component storage independently in every registry that may contain those components.
3. Register `AbilityActivationQueue` independently in every registry that processes activations.
4. Change ability, effect, attribute, and movement algorithms to accept explicit definition references.
5. Resolve definitions once in schedule adapters from `ctx.Registries.Global->Resources`.
6. Fail loudly when required global definitions are missing.
7. Do not fall back to zone-local definitions.
8. Update game startup and template wiring so games extend global definitions before simulation begins.
9. Extend `SceneSerializationContext` for gameplay tag and attribute definitions.
10. Update detached zone recipes to carry those definitions.

Tests must prove shared ids across zones, no copied definition registries, correct game extension behavior, and identical synchronous and asynchronous scene loading.

Stop condition:

- session definitions exist only in global resources
- zone resources contain no copied definition registries
- serializers use explicit context
- CI is green

## Phase 6: Delete the entity-store resource system

1. Delete the resource map and all resource APIs from the current `World`.
2. Delete resource destruction and move handling from that type.
3. Remove resource-related includes.
4. Search the repository for every deleted API.
5. Update ECS, audio, movement, physics, transform, serialization, and migration documentation.

Stop condition:

- entity storage owns no resources
- all resource ownership is visible on `Registry`
- repository search finds no old resource API
- CI is green

## Phase 7: Perform the atomic public rename

In one mechanical slice:

1. Rename `World` to `EntityStore`.
2. Rename `World.h` to `EntityStore.h`.
3. Rename `Registry::Components` to `Registry::Entities`.
4. Rename the private entity identity table to avoid a naming collision.
5. Update engine, runtime, serializers, template, examples, benchmarks, tests, editor call sites, documentation, and `CLAUDE.md`.
6. Delete old headers and names in the same commit.
7. Add no aliases or compatibility members.

Correct layering description:

```text
ZoneRuntime owns Registries.
Registry owns ResourceStore and EntityStore.
EntityStore owns component catalog, entity identity, and archetype storage.
```

Stop condition:

- no `class World`
- no `<ecs/World.h>`
- no `Registry::Components`
- no compatibility alias
- CI is green

## Phase 8: Decompose `EntityStore` internally

Extract only concrete mechanisms whose state and invariants already exist.

### `ComponentCatalog`

Owns stable identity lookup, compact ids, metadata, layout, tag classification, lifecycle callbacks, registration freeze, and component budget enforcement.

### `ArchetypeStorage`

Owns archetypes, signature lookup, archetype creation, chunk layout construction, and trusted query traversal.

### `EntityStore`

Retains entity identity, the catalog, archetype storage, structural movement, typed and raw component access, creation and destruction, guards, epochs, structural versioning, and lifecycle dispatch.

Rules:

1. Move non-template implementation into source files.
2. Keep templates in focused headers or included implementation files.
3. Remove public `GetArchetypes`.
4. Give cached queries the narrowest trusted access required.
5. Make query scope mutation private.
6. Keep raw mutation public only for serialization and editor inspection boundaries.
7. Add no interfaces, factories, providers, or strategy objects.
8. Extract no class that merely forwards calls back to `EntityStore`.

Stop condition:

- each type owns distinct state and invariants
- archetype containers are not public
- public ECS capability is unchanged
- CI is green

## Phase 9: API and performance pass

Delete forwarding methods with no policy, duplicate typed and raw implementations, public methods used only by query internals, stale migration comments, temporary test accessors, dead includes, and stale resource terminology.

Retain typed game access, type-erased serializer and editor access, explicit structural mutation, cached queries, change epochs, structural versioning, and only the pointer-cache access that real consumers require.

Performance checks:

- no new per-entity allocation
- no new allocation for ordinary component moves beyond existing archetype behavior
- no `type_index` lookup inside per-entity loops
- no resource lookup inside per-entity loops
- cached query behavior remains unchanged
- serial and parallel registry behavior remains deterministic
- ECS benchmark has no unexplained repeatable regression above five percent

Stop condition:

- benchmark results are reviewed
- API surface reflects actual consumers
- CI is green

## Phase 10: Final branch validation

The branch is complete only when:

### Ownership

- each registry owns exactly one resource store
- entity storage owns no resources
- resources outlive component hooks
- session definitions exist once
- registry-local mutable state is isolated
- system-owned services remain outside registries

### Naming

- `EntityStore` is the only ECS facade name
- `Registry::Entities` is the only registry entity member
- `ResourceStore` is the only type-indexed registry resource owner
- no compatibility names remain

### API shape

- lifecycle hooks cannot structurally mutate entities
- systems receive specific dependencies
- serializers receive explicit definitions
- archetype containers are not public
- no service locator, global singleton, interface hierarchy, or adapter stack exists

### Validation

- configure passes
- full build passes
- full test suite passes
- detached zone loading passes
- game-module loading and unloading pass
- editor target compiles after mechanical migration
- template and examples compile
- ECS benchmark is reviewed
- searches for retired names return no production matches

### Documentation

Update `CLAUDE.md`, ECS decisions, component lifecycle documentation, migration records, core systems map, ability and movement documentation, audio runtime documentation, transform documentation, and the PR description.

Comments describe only the final mechanism and its invariants. They contain no task history, phase references, editorial commentary, or em dashes.

## Expected final ownership

```text
ZoneRuntime
  Registry
    ResourceStore
    EntityStore
      ComponentCatalog
      EntityRegistry
      ArchetypeStorage
```

Common runtime code sees `Registry`. Component algorithms see `EntityStore`. Hooks see `ResourceStore`. Systems with global definitions receive those definitions explicitly.

The architecture becomes smaller by deleting ambiguity rather than hiding it behind another abstraction.
