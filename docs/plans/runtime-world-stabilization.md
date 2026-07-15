# Runtime World Stabilization Plan

Status: active implementation plan

This plan is runtime-only. Kyusu editor decomposition lives on the separate `agent/editor-decomposition-plan` branch.

## Goal

Stabilize the runtime contracts currently concentrated inside `ecs::World`, then reduce `World` to an honest ECS storage facade owned by `Registry`.

The order matters: correctness first, ownership second, naming and extraction third. A large rename before the invariants are executable would only spread the same defects across more files.

## Confirmed problems

### ECS change epochs are not advanced by the runtime

`World::CurrentFrame()` and `AdvanceFrame()` back `Changed<T>` and transform column-version logic, but the engine frame loop does not advance registry worlds. Tests and benchmarks do so manually.

Column version zero is also the documented "never written" sentinel, while a default `World` begins at frame zero. Initial registry construction therefore wrote component columns at the sentinel value and made them invisible to `Changed<T>` with reference frame zero.

The first implementation slice gives runtime registries a nonzero initial epoch, makes subsequent epoch advancement a `ZoneRuntime` responsibility, and invokes it once per outer frame after scheduled end-frame systems have consumed the current epoch.

### Entity and registry destruction skip component removal hooks

`DestroyEntity` removes archetype storage without dispatching `ComponentTraits<T>::OnRemove`. Registry and zone destruction therefore skip asset release, audio voice shutdown, caption cleanup, and other documented component lifetime work.

### Registry-local resources have two owners

`Registry` owns `ResourceRegistry`, while its embedded `World` owns a second type-indexed resource map. Lifecycle hooks receive only `World&`, forcing some registry-local dependencies into the wrong owner.

### Query mutation guards are incomplete

Chunk queries manually push and pop query scope without RAII. `World::ForEachComponent` does not enter query scope at all, so structural mutation during iteration is not uniformly prevented.

### Migration compatibility remains in the runtime model

`RegistryEntityFacade` and `World`'s legacy store bag remain after the archetype ECS migration is otherwise described as complete.

## Phase 0: Runtime correctness

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

1. Remove `RegistryEntityFacade` after migrating callers.
2. Remove the legacy store bag from `World`.
3. Rename `World` to `EntityStore` or another mechanically honest name.
4. Rename `Registry::Components` to `Registry::Entities`.
5. Remove forwarding methods that add no policy, including `EngineSchedule::BuildFrameView` if no new responsibility appears.

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

## First implementation slice

The first landed change is deliberately small:

- make every runtime `Registry` begin at ECS epoch one so zero remains the unwritten sentinel
- add `ZoneRuntime::AdvanceFrameEpochs()`
- advance the global registry and every loaded zone exactly once, regardless of participation flags
- call it from the engine's `EndFrame` phase after scheduled end-frame systems and before ending the frame view
- add runtime tests covering initial `Changed<T>` visibility plus global, active, dormant, and repeated advancement

This fixes the immediate `Changed<T>`/column-version ownership hole without beginning the larger lifecycle and resource migration in the same patch.
