# Runtime World Stabilization Plan

Status: active implementation plan; runtime correctness and compatibility cleanup landed

This plan is runtime-only. Kyusu editor decomposition lives on the separate `agent/editor-decomposition-plan` branch.

## Goal

Stabilize the runtime contracts currently concentrated inside `ecs::World`, then reduce `World` to an honest ECS storage facade owned by `Registry`.

The order matters: correctness first, ownership second, naming and extraction third. A large rename before the invariants are executable would only spread the same defects across more files.

## Landed invariants

- The runtime owns ECS epoch advancement, and structural additions stamp their destination columns.
- Query and component iteration scopes restore their guards and publish partial writes when callbacks throw.
- Component metadata is the sole authority for type-erased layout and lifecycle dispatch.
- Removal hooks run for direct and buffered removal, entity destruction, registry clear, zone destruction, and engine shutdown.
- Registry entities are cleared while registry resources, engine services, and game-owned assets are still alive.
- The legacy store bag and `RegistryEntityFacade` have been removed, with callers migrated directly to `World`.

## Remaining confirmed problem

### Registry-local resources have two owners

`Registry` owns `ResourceRegistry`, while its embedded `World` owns a second type-indexed resource map. Lifecycle hooks receive only `World&`, forcing some registry-local dependencies into the wrong owner.

## Phase 0: Runtime correctness - complete

1. Make registry epoch initialization and advancement explicit and engine-owned.
2. Add runtime tests proving initial writes are visible and global, active, and dormant registries advance exactly once.
3. Add RAII query guards to every iteration path.
4. Store type-erased lifecycle operations in component metadata.
5. Route component removal, entity destruction, registry clear, and zone destruction through one lifecycle path.
6. Add teardown tests for direct destruction, buffered destruction, registry clear, and zone destruction.
7. Add an explicit world-session clear path so game-owned resources outlive component cleanup.

## Phase 1: Unify registry resources

1. Introduce a narrow `ComponentLifecycleContext` owned by `Registry`.
2. Make component hooks consume lifecycle context rather than the entire ECS facade.
3. Move physics scenes, transform caches, asset bindings, audio bindings, and gameplay queues to `Registry::Resources`.
4. Remove the resource map and resource API from `World`.
5. Reorder registry members or explicitly clear entities so resources outlive all component hooks.
6. Add duplicate-registration and teardown-order tests.

## Phase 2: Finish the ECS migration

Completed:

1. Remove `RegistryEntityFacade` after migrating callers.
2. Remove the legacy store bag from `World`.

Remaining:

1. Rename `World` to `EntityStore` or another mechanically honest name.
2. Rename `Registry::Components` to `Registry::Entities`.
3. Remove forwarding methods that add no policy, including `EngineSchedule::BuildFrameView` if no new responsibility appears.

Do this atomically. Do not leave permanent redirector headers or type aliases.

## Phase 3: Internal ECS decomposition

Keep one concrete ECS-facing facade, but extract concrete mechanisms:

### `ComponentCatalog`

Owns component identity, compact ids, layout metadata, tag classification, lifecycle callbacks, and registration freeze.

### `ArchetypeStorage`

Owns archetypes, signature lookup, chunk allocation, and layout construction.

### `EntityStore`

Coordinates entity identity, structural movement, typed and type-erased access, structural versioning, query guards, change epochs, and lifecycle dispatch.

No interface hierarchy or factory layer is warranted.

## Phase 4: Resource scope decisions

Classify state explicitly:

- engine or game-global immutable definitions, such as ability, effect, attribute, and gameplay-tag catalogs
- registry-local state, such as camera services, physics scenes, transform caches, audio bindings, and queues
- scheduled-system-owned state, such as the shared physics world and collision-shape cache

Do not duplicate immutable gameplay definition registries in every streamed zone unless per-zone identity is an intentional requirement.

## Next implementation slice

Unify registry-local resource ownership before renaming the ECS facade. The slice must establish one owner for lifecycle dependencies, preserve teardown order, and remove `World`'s resource map without introducing an interface hierarchy or service locator.
