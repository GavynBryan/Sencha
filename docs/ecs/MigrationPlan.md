# Sencha Archetype ECS Migration Plan

## Purpose

Sencha currently stores components in per-type sparse sets (`SparseSet<T>`), wrapped by `IComponentStore` / `ITypedComponentStore<T>` and owned by `ComponentRegistry`. This document specifies the migration to an **archetype-based ECS**: entities grouped by component signature, components stored in fixed-size chunks as parallel column arrays.

This migration is being executed on a dedicated branch as an **in-place refactor**. There is no coexistence with the old ECS. The tree will not compile partway through; that is expected.

The implementing agent reads this document and executes the plan. It is the full context.

---

## Motivation

Sencha targets survival horror, souls-likes, and 3D metroidvanias in the near term, and open-world games long term. The stated thesis is "one of the most performant open-source engines on the market." Sparse-set-per-component storage has a ceiling that archetype storage does not:

- **Joined iteration is expensive under sparse-set.** Every "entities with {A, B, C}" query is a primary sweep plus N-1 sparse hops per entry. At 100k+ entities with multi-component queries, this dominates CPU time in the render-extract / transform-propagate hot paths.
- **Component-column SoA becomes the default layout.** Archetype chunks store each component type as a contiguous column within the chunk. A system iterating one component touches only that column. (Field-level SoA — splitting a single component's fields into separate arrays — is **not** part of v1. It may later be introduced via an explicit storage trait for selected engine components, but it is not free and not assumed.)
- **Parallel iteration requires disjoint mutable slices.** Archetype chunks are naturally parallelizable; sparse-set dense arrays are shared mutable state across all consumers.
- **GPU-driven rendering wants contiguous per-instance data on CPU that can be `memcpy`ed to a GPU buffer.** Archetype chunks are already that shape; sparse sets require a gather pass.

Sencha is pre-v0.5 with no shipping deadline. The ECS surface is small today (transforms, static meshes, cameras). The refactor cost is never lower than now.

---

## Non-Negotiable Design Axioms

These are the rules every decision is measured against. They exist because prior art (Flecs, EnTT, Unity DOTS, Bevy) has shown what happens when any of them is relaxed. If a proposed implementation violates one of these, the proposal is wrong — rethink, don't compromise.

### A1. The core is closed; extension is open (OCP)

Adding a new component, a new system, or a new query **must not require modifying any file in ECS core**. New components are POD structs registered via a template call at module init. New systems are free functions or classes in any module that declare their query signatures and are invoked by the scheduler. No central switch-on-type, no central registration header listing every component.

### A2. Substitutability at the storage level (LSP)

No component type is "special." `LocalTransform`, `WorldTransform`, `Parent`, `StaticMesh`, `CameraComponent`, and any hypothetical game-code `Health` are all stored, queried, and mutated through the same primitives. Transforms are not privileged with bespoke storage or bespoke APIs. The ECS core does not know the names of any concrete component types.

### A3. Hot paths are free functions over typed data

Query iteration resolves to typed spans over chunk columns. Inside an inner loop there are: no virtual calls, no `std::variant` dispatch, no `std::any`, no `std::type_index` lookups, no hash map probes, no heap allocations. Type erasure is allowed only at structural-change boundaries (command buffer flush, archetype graph traversal).

### A4. Structural changes are explicit and batched

Adding or removing a component from an entity moves that entity between archetypes. This is never allowed to happen mid-query. All structural mutations from inside systems go through a command buffer and flush at explicit scheduler phase boundaries. The API makes illegal mutations fail loudly (assert in debug, documented as UB in release with a comment at the call site).

### A5. One storage model, one mental model

Every component lives in archetype chunks. There is no hybrid where some components are sparse and some are archetype. There is no "shared component" concept. There is no "chunk component" concept distinct from regular components. If per-archetype data is needed, it is a regular component on a representative entity, or a resource on `World`. One mechanism.

### A5a. Tag components express stable facts, not transient state

Adding or removing a tag component is a **structural change** — the entity moves between archetypes, even though tag columns carry no per-entity data. Tag components are therefore appropriate for facts that persist for many frames (`Player`, `Enemy`, `Static`, `EditorOnly`, `NoRender`) and inappropriate for high-frequency flags that flip every frame (dirty, changed, just-moved, visible-this-frame).

High-frequency "what happened this frame" state is expressed through the `Changed<T>` query filter, which reads per-chunk per-column version counters without moving entities. If a system needs to filter on "world transform was updated this frame," the correct mechanism is `Changed<WorldTransform>`, not a `WorldChanged` tag component.

Violating this rule produces archetype churn: thousands of entities moving between archetypes twice per frame just to flip a bit. That churn dominates the cost the archetype storage was meant to eliminate.

### A6. No hidden machinery

If a line of code triggers an allocation, an entity move, or a structural change, the call site reads that way. `Get` never auto-creates. `Add` never auto-registers a component type. Queries do not silently create archetypes. Lifecycle hooks fire synchronously at command-buffer flush, not on a deferred event bus.

### A7. Sympathy for hardware without esoteric code

Chunk sizes, column layouts, and alignment are chosen for L1/L2 cache behavior on 64-byte cache lines. This is documented in one header, in plain English, with the numbers and the reasoning. No hand-rolled SIMD intrinsics in core, no `__builtin_expect` tuning, no platform-specific branches. A human reading the file should understand why the numbers are what they are within five minutes.

### A8. Legibility beats cleverness

Names describe intent. Files are grouped by concept. `decisions.md` captures the "why" behind every non-obvious choice. When a human or AI returns to this code after six months of absence, they should be able to orient themselves from the docs and the code alone — not by reconstructing context from git history or external references.

---

## Pitfalls from Prior Art and Required Mitigations

Each row is a failure mode observed in published archetype ECS implementations. The mitigation column is mandatory; it is not a menu.

| # | Pitfall | Where it has bitten | Required mitigation in Sencha |
|---|---------|---------------------|-------------------------------|
| P1 | Structural change mid-iteration invalidates pointers and iterators | Early Flecs, still a footgun in Unity DOTS | Command buffers are the only way to mutate structure from inside a system. Queries hand out const chunk views. Direct `World::AddComponent` / `RemoveComponent` from inside an active query is an assertion failure in debug builds. |
| P2 | Archetype explosion: too many unique component signatures blow up memory and query cost | DOTS with heavy tag-component use | Tag components (zero-size markers) are stored as **signature bits only**, not as per-entity columns. A diagnostic warns when archetype count crosses a configurable threshold (default: 256). |
| P3 | Query cost grows linearly with archetype count | EnTT (mitigated via groups), Flecs (mitigated via query cache) | Queries are durable, cached objects. A query subscribes to archetype creation events and maintains its matching archetype list incrementally. First acquisition builds; subsequent iterations walk the cached list. |
| P4 | Raw component pointers escape queries and get invalidated by later structural changes | Universal | No raw component pointers cross a query boundary. Inside a query, column spans are valid for the duration of that chunk's iteration only. Cross-frame or cross-system references use `EntityId` (generational) and resolve through `World`. |
| P5 | Magic chunk sizes that users must know | DOTS (16KB, famously) | Chunk size is a named compile-time constant in one header, with a comment explaining the choice (L2-friendly, fits N rows of a typical archetype). Not user-tunable without editing that one file. |
| P6 | Component lifecycle hooks become untraceable spaghetti | Flecs observers at scale | Lifecycle hooks are opt-in per component type via `ComponentTraits<T>` specialization, not a global observer bus. A hook runs synchronously during command-buffer flush. `grep ComponentTraits` lists every component with hooks. |
| P7 | Shared-component / chunk-component concepts bifurcate the mental model | DOTS | Not supported. If per-archetype data is needed, use a regular component on a representative entity, or a resource on `World`. See A5. |
| P8 | Entity generation drifts out of sync with storage | Universal | Generation lives in the entity registry only. Storage reads `EntityIndex`, never generation. Generation checks happen at API boundaries: `World::TryGet(EntityId)` validates the generation, extracts the index, then dispatches. |
| P9 | Query DSLs drift toward complexity | Flecs | One query form: `Query<Read<A>, Write<B>, Without<C>, Changed<D>, With<TagE>>`. No predicate lambdas inside query definitions. Runtime filtering happens in the system body, not the query. |
| P10 | Serialization tightly coupled to storage layout | DOTS, painfully | Serialization iterates by component type, through a `World::ForEachComponent<T>()` or equivalent type-keyed accessor. Save format is decoupled from chunk layout. The existing `IComponentSerializer` contract is preserved in spirit. |
| P11 | Change detection via global version counters grows stale or incorrect | Bevy (iterated multiple times to get right) | Change detection is **per-column per-chunk**: each chunk stores a last-written-frame counter per column. `Changed<T>` filters at chunk granularity — skip a whole chunk when its column is unchanged. No per-entity version counter. |
| P12 | Entity destruction during iteration corrupts state | Universal | `DestroyEntity` is a command buffer operation. Direct destruction outside of startup/teardown phases is an assertion failure. |
| P13 | Archetype transitions during bulk structural change thrash memory | DOTS | The command buffer groups operations by target archetype during flush, so a batch of "add component X to N entities" resolves to one bulk move per source archetype, not N individual moves. |
| P14 | Query iteration doesn't parallelize cleanly | Less common, but real when retrofit | Queries expose a chunk-level iteration API as the primary entry point. A single-threaded convenience wrapper exists but desugars to chunk iteration. The data shape is parallel-ready even before a job system exists. |

---

## Target Architecture

### Core concepts

- **EntityId**: a generational handle (existing `EntityId` struct is preserved). Index identifies a slot; generation distinguishes reuses.
- **ComponentId**: a stable small-integer id assigned at component registration. Identifies a component type within a world. Stored as `uint16_t` to leave headroom for future expansion.
- **ArchetypeSignature**: a fixed-size bitset over component ids. **v1 budget: 256 registered component types per world.** The `ComponentId` type width (16-bit) exceeds this deliberately; signature bitset width is the enforced v1 limit. Two entities are in the same archetype iff their signatures are equal. A diagnostic warns when the registered component count approaches the budget.
- **Chunk**: a fixed-size block of memory holding rows of one archetype. Columns are parallel arrays, one per component in the signature. A chunk also stores the owning `EntityIndex` per row, a per-column last-written-frame counter, and row count / capacity.
- **Archetype**: the metadata for a signature (column layout, row stride, per-chunk capacity) plus the list of its chunks.
- **World**: owns entity registry, archetype table, archetype graph, query cache, resources, and command-buffer pool. The single entry point for user code.
- **Query**: a durable object parameterized by accessors. Caches matching archetypes. Provides chunk-level and entity-level iteration.
- **CommandBuffer**: records structural mutations during system execution. Flushed by the scheduler at phase boundaries.
- **ComponentTraits\<T\>**: per-component opt-in specialization point for lifecycle hooks (`OnAdd`, `OnRemove`) and storage hints (alignment overrides, etc.). Default specialization is trivial.

### Query accessors

The query type parameter list is the **complete and public** contract of what a system reads and writes. A human reviewer understands the data dependencies of a system from its query signature alone.

- `Read<T>` — const access to component T. Archetype must have T.
- `Write<T>` — mutable access to T. **Granting `Write<T>` access to a chunk bumps that chunk's column-version counter for T**, regardless of whether individual rows are actually modified. This is conservative-bump semantics: a system that holds `Write<T>` for a chunk but writes nothing will still cause `Changed<T>` filters to match that chunk this frame. Precise per-row tracking is not supported in v1 (it requires either mutation proxies that violate A3, or explicit mark-dirty calls that violate A6). Archetype must have T.
- `With<T>` — archetype must have T, but no accessor is provided. Used for tag components and for expressing dependencies on components the system doesn't read.
- `Without<T>` — archetype must **not** have T.
- `Changed<T>` — chunk-level filter driven by **frame-clock semantics**, not query-local "last run" semantics. Each chunk stores a per-column last-written frame counter; the query compares it against an explicit reference frame. Scheduler-invoked systems default the reference frame to the previous completed frame (so the filter means "changed since the end of the previous frame"). Manual query use may pass an explicit reference frame. Running the same query twice within a frame produces identical results. Conservative: may include chunks whose `Write<T>` access didn't actually modify rows.

Queries expose two iteration forms:

- **Chunk iteration** (primary): the system receives a view over one chunk at a time, with column spans for each `Read`/`Write` accessor. Inner loop is the system's responsibility and is a tight cache-friendly sweep.
- **Entity iteration** (convenience): a wrapper that desugars to chunk iteration and invokes a per-entity callable. Exists for ergonomics; not used in hot paths.

### Structural changes

From inside a system, all structural changes go through a command buffer obtained from the scheduler or world:

- `cmds.AddComponent<T>(entity, value)` — queues adding T to entity's archetype at flush.
- `cmds.RemoveComponent<T>(entity)` — queues removal.
- `cmds.DestroyEntity(entity)` — queues destruction.
- `cmds.CreateEntity(initialComponents...)` — queues creation with an initial component set.

Direct `World::AddComponent` / `RemoveComponent` / `DestroyEntity` calls are legal only during startup/teardown phases (no queries in flight). Calling them while a query is active is an assertion failure in debug builds.

### Component registration

```
world.RegisterComponent<Transform>();
world.RegisterComponent<StaticMesh>();
world.RegisterComponent<TagDirty>();  // zero-size tag
```

Registration assigns a `ComponentId`, records size/alignment, and inspects `ComponentTraits<T>` for opt-in hooks. Component ids are stable within a world's lifetime.

**Registration timing is restricted in v1**: all component types must be registered during world / module initialization, before the first entity is created. Attempting to register a component after any entity exists is an assertion failure in debug builds. This restriction dramatically simplifies the first implementation (signature bitset sizing, query cache invalidation, archetype metadata layout) and can be loosened later if a real need appears. Sencha is an engine, not a dynamic scripting VM; component types are known at module init.

### Lifecycle hooks via ComponentTraits

Lifecycle behavior (e.g., asset retain/release for `StaticMesh`) is expressed by specializing `ComponentTraits<T>` in a header near the component type. The trait exposes constexpr flags (`HasOnAdd`, `HasOnRemove`) and static member functions. The ECS core dispatches through `if constexpr` — zero cost for components without hooks.

Hooks run synchronously during command-buffer flush, in the order commands were recorded. **In v1, lifecycle hooks must not perform structural ECS mutations.** Hooks may touch resources, retain/release external assets, emit logs, and mutate already-present components on the same entity (within the lifecycle-hook rules around pointer validity). Hooks may **not** call `AddComponent`, `RemoveComponent`, `CreateEntity`, or `DestroyEntity`, directly or via a command buffer. If cascading structural behavior is needed, the system that originated the command records the cascade explicitly before flush. This restriction prevents lifecycle hooks from becoming observers by another door, which violates A6.

### Resources

`World` also owns **resources** — singleton objects keyed by type, not associated with any entity. This subsumes what `ResourceRegistry` does today. Used for things like the frustum culler's camera data, the frame clock, asset caches. Systems declare resource dependencies alongside queries (or access them directly via `world.GetResource<T>()`).

---

## Migration Phases

Each phase has explicit entry conditions, deliverables, and exit criteria. Do not start phase N+1 until phase N's exit criteria are green.

### Phase 0 — Spike (throwaway)

**Goal**: validate the core API ergonomics and the perf thesis before committing to the rewrite.

**Location**: a standalone directory outside the engine tree (suggested: `ecs_spike/` at repo root). Nothing from this phase lands in the engine tree.

**Scope**:
- Minimal `World`, `Archetype`, `Chunk`, one `Query<Read<A>, Read<B>, Write<C>>`.
- `CommandBuffer` with `AddComponent`, `RemoveComponent`, `DestroyEntity`, and flush.
- Tag-component support (zero-size, signature-only).
- Per-column change detection (conservative-bump on `Write<T>` access).
- Two micro-benchmarks:
  - **Iteration throughput**: 100k entities, two components, one system iterating the join. Compare to an equivalent sparse-set join.
  - **Structural churn**: create 100k entities with components `{A, B}`; add component `C` to 10k of them, then remove `C`; measure command-buffer flush time. Compare naive per-entity moves vs. grouped-by-source/target-archetype batched moves. This catches the failure mode where iteration wins but structural changes stall the frame.

**Exit criteria**:
1. Iteration benchmark shows **at least 3× throughput** on joined queries vs. the sparse-set baseline at 100k entities.
2. Structural-churn benchmark: flush time for 10k component adds + 10k removes. **Soft target: comfortably under 2 ms in an optimized build on the development machine.** This is not a hard portability guarantee, but exceeding it requires a `decisions.md` entry describing the cause and the mitigation plan before proceeding to Phase 1. Record the absolute number regardless of outcome.
3. The spike's query API reads cleanly without needing explanation. Write three example systems against it; if any of them require a workaround or a comment explaining "why the API forces this shape," iterate on the API before proceeding.
4. `docs/ecs/decisions.md` exists and records every non-obvious design decision made during the spike, each with a one-paragraph rationale.

If criterion 1 or 2 fails, halt and reassess before touching the engine tree.

**If the spike fails criterion 1 or 2, halt the migration and reassess.** The spike is cheap; being wrong in the main tree is expensive.

### Phase 1 — Replace the core, in place

Entry condition: Phase 0 complete, `decisions.md` reviewed.

**Goal**: land the new ECS in the engine tree. Delete the old ECS. The tree will not compile at the end of this phase. That is the expected state.

**Land**:
- `engine/include/ecs/` — public headers: `World`, `EntityId` (kept from current tree or re-homed), `ComponentId`, `ArchetypeSignature`, `Chunk`, `Archetype`, `Query` and accessors (`Read`, `Write`, `With`, `Without`, `Changed`), `CommandBuffer`, `ComponentTraits`, `Resource` support.
- `engine/src/ecs/` — implementations.
- `engine/test/ecs/` — full unit-test coverage of every core primitive. Tests cover: component registration (including rejection of registration after first entity), entity create/destroy, add/remove component, archetype transitions, all query accessor combinations, command-buffer semantics, flush ordering, lifecycle hook execution, lifecycle hook structural-mutation rejection, tag components, change detection (per-column, per-chunk, frame-referenced), and resource storage.

**Delete**:
- `engine/include/world/IComponentStore.h`
- `engine/include/world/ITypedComponentStore.h`
- `engine/include/world/ComponentRegistry.h`
- `engine/include/world/SparseSetStore.h`
- `engine/include/world/transform/TransformStore.h`
- `engine/include/render/StaticMeshComponentStore.h` and `.cpp`
- `engine/include/core/batch/SparseSet.h` (may be retained as a private implementation detail of the new ECS if used for the entity-index → location map; decide during implementation based on whether a simpler structure suffices)
- `engine/include/core/batch/DataBatch.h`, `DataBatchKey.h`, `InstanceRegistry.h`, `DataBatchHandle.h` — audit each for live usage before deletion; `DataBatch` has no live instantiations in current hot paths per prior investigation, but verify.

**Preserve** (may be rehomed, but the concepts survive):
- `EntityId` and `EntityRegistry` — generational entity ids are kept. Integration with `World` is an implementation choice.
- The public shape of `Registry` (the outer object engine code holds) may survive with its `Components` field replaced by a `World` reference, or it may be absorbed into `World` directly. Decide based on what reads cleaner to downstream code.
- `IComponentSerializer` contract — serialization *interface* is preserved; its backing changes to iterate via `World`.
- `TransformHierarchyService` and `TransformPropagationOrderService` — these may survive as query-backed caches or be absorbed into the propagation system. Decide during Phase 3.

**Exit criteria**:
- All new ECS unit tests pass.
- The old ECS files listed above are deleted from the branch.
- `engine/` does not compile when linked against the rest of the tree. This is expected.

### Phase 2 — Make it compile

**Goal**: restore compilation. Port component type definitions and their registration/creation sites. No system logic is rewritten yet — just enough scaffolding to let the tree build.

**Component splits and renames**:

The `TransformComponent<T>` bundle splits into single-responsibility components:

- `LocalTransform` — the authoritative local TRS. Serialized.
- `WorldTransform` — the derived world TRS, written by propagation. Not serialized (reconstructed on load).
- `Parent` — optional component carrying the parent `EntityId`. An entity has a parent iff it has this component. Replaces whatever hierarchy linkage lived on the old transform component.

**Dirty / changed state is *not* expressed as tag components.** Per A5a, high-frequency transient state as tag components produces archetype churn. Instead:

- "This entity's local transform was written this frame" is expressed as `Changed<LocalTransform>` — a chunk-level filter driven by the conservative column-version bump when `Write<LocalTransform>` access is granted.
- "This entity's world transform was updated by propagation this frame" is expressed as `Changed<WorldTransform>` — downstream systems (render extraction, physics sync) filter on this.

No `LocalDirty`, no `WorldChanged`, no transient tag components. The `TransformPropagationOrderService::WorldChanged` bitset becomes unnecessary — `Changed<WorldTransform>` replaces it.

Rationale: single responsibility per component (LSP, A2), one change-detection mechanism for the whole engine (A5), and no structural churn from frame-local state flips.

`StaticMeshComponent` is preserved as a component type. Its asset retain/release lifecycle (currently in `StaticMeshComponentStore::Attach/Detach`) moves to a `ComponentTraits<StaticMeshComponent>` specialization with `OnAdd` and `OnRemove` hooks.

`CameraComponent` (whatever shape it has today) is ported as a regular component.

**Update call sites**:
- `DefaultZoneBuilder` — component additions go through `World::AddComponent` (or command buffers if called during a system phase).
- `SceneSerializer` / `ComponentSerializer` — backing is redirected to `World::ForEachComponent<T>()` or equivalent. The per-type traits in `ComponentStorageTraits` are updated accordingly. Serialization output format may shift if necessary for correctness, but prefer preserving it.
- `DefaultRenderPipeline` — switches from `Components.TryGet<...Store>()` lookups to query acquisition on `World`.
- `PropagateTransforms` free function — signature updates to accept a `World&` instead of the old triplet; body is stubbed or left incomplete (logic port happens in Phase 3).
- Editor modules — component reads/writes go through `World` / `CommandBuffer`. The recent edit-session and cube-manipulation code is updated.

**Exit criteria**:
- The engine tree compiles.
- Tests compile. They may not all pass yet; failing system-level tests are expected and addressed in Phase 3.

### Phase 3 — Make it correct

**Goal**: port system logic. Tests pass. Examples run.

**Systems to port**:

1. **Transform propagation**. Rewrite `PropagateTransforms` as a system over the new component split:
   - Query 1 (ordering): determine parent-before-child traversal. Either absorbed into a propagation resource (the spiritual successor of `TransformPropagationOrderService`), or computed per-frame from a `Read<Parent>` query if benchmarks allow. Decide based on measurement.
   - Query 2 (propagation): `Read<LocalTransform>, Read<Parent>, Write<WorldTransform>`. Writing `WorldTransform` bumps its column version, which downstream systems consume via `Changed<WorldTransform>`.
   - **Hierarchy dirtiness is non-negotiable**: a child is dirty if its own `LocalTransform` changed **or** any ancestor's `WorldTransform` changed. `Changed<LocalTransform>` alone is insufficient for parented entities — a parent moving while the child's local is unchanged still requires recomputing the child's world. Propagation must propagate dirtiness down the tree. Implementation is a judgment call (hierarchy-dirty traversal resource, frame-local dirty set, or full traversal if cheap enough at Sencha's scales), but the invariant is mandatory.
   - Downstream consumers must treat `Changed<WorldTransform>` as **chunk-conservative, not entity-exact** — a chunk matching the filter may contain unchanged rows alongside changed ones. Do not assume per-entity precision.
   - Hierarchy service (`TransformHierarchyService`): survives as a query-backed index if it's faster than recomputing, or is deleted if per-frame recomputation is cheap enough. Measure both.

2. **Render extraction**. Rewrite as a chunk query: `Read<WorldTransform>, Read<StaticMeshComponent>` (plus cached-bounds/matrix components if they're introduced — see below). Inline frustum culling in the same pass. Emit `RenderQueueItem`s.

3. **Frustum culling**. Folded into render extraction. The standalone `FrustumCullingSystem::Cull` post-pass goes away.

4. **Render queue item**. In v1, `RenderQueueItem` continues to carry copied render-critical data (world matrix, world bounds, mesh handle, material handle, sort key). It does **not** reference live ECS storage via chunk pointer + row index. Copying maintains a clean decoupling between extraction and submission: any structural change between extract and submit cannot invalidate queue items, and the renderer can be parallelized or deferred without a lifetime contract against live chunks. The sort win the original RFC targeted comes from sorting on a small `SortKey` — the rest of the item payload is loaded only by the submission code that already needs it. Chunk-reference queue items are a possible later optimization; **only pursue after Phase 4 benchmarks demonstrate the copy cost matters** and after the lifetime contract is carefully designed.

5. **Cached render-side components (optional, decide during implementation)**: `CachedWorldMatrix` and/or `CachedWorldBounds` may be introduced as components written during propagation / on mesh change. Alternatively, if `Transform3f::ToMat4()` and the 8-point AABB transform are cheap enough to compute inline during render extraction (now that we're on archetype chunks and no longer doing sparse hops), skip the cache entirely. **Measure before deciding.** Adding cached components is easy; removing them later is harder.

6. **Editor transform manipulation**. The cube edit-session and gizmo code are re-pointed at `CommandBuffer` for writes and queries for reads.

**Exit criteria**:
- All existing tests pass: `TransformPropagationTests`, `SceneSerializerTests`, `DefaultZoneBuilderTests`, `TransformServiceTests`, and every other test that previously depended on the old ECS.
- `CubeDemo` renders identically to pre-migration.
- `TransformHierarchyStressTest` runs to completion.
- Editor cube manipulation works.
- No references remain to deleted old-ECS types.

### Phase 4 — Measure and document

**Goal**: verify the perf thesis and update documentation.

**Benchmarks** (record numbers in `docs/ecs/decisions.md`):
- Transform propagation throughput vs. pre-migration baseline.
- Render extraction throughput vs. pre-migration baseline.
- `RenderQueueItem` sort time vs. pre-migration.
- Archetype count and memory footprint under representative scenes.

**Documentation**:
- `docs/ecs/overview.md` — the document a new human reads first. Explains entities, components, archetypes, chunks, queries, systems, command buffers. Worked example of writing a new component and a new system.
- `docs/ecs/queries.md` — query cookbook. Every accessor combination, with a short example.
- `docs/ecs/component-traits.md` — how to add lifecycle hooks, when to use them, when not to.
- `docs/ecs/command-buffers.md` — semantics, flush ordering, nested commands, the structural-change invariants.
- `docs/ecs/decisions.md` — finalized with benchmark numbers and any design choices made during Phases 1–3.

**Exit criteria**:
- Benchmarks recorded.
- All five docs above exist and are accurate to the landed code.
- A fresh reader can write a new component + system from the docs alone without reading ECS core source.

### Phase 5 — Parallelization (deferred)

**Do not start this phase as part of this migration.** It is documented here so the agent knows where it fits.

Once Phase 4 is complete and benchmarks are in hand, if profiling indicates query iteration is a significant cost, chunk-level parallelization is the next step. Archetype chunks parallelize cleanly — a job system can hand each worker N chunks and trust that disjoint chunks never alias. This is a separate subproject with its own design doc.

---

## Guardrails

These three exist to prevent scope creep and hidden complexity during execution.

### G1. `docs/ecs/decisions.md` is mandatory reading for every ECS-core commit

Every non-obvious choice gets a one-paragraph entry: what was decided, what alternatives were considered, why this won. Future maintainers (human or AI) read this before changing core. It is how context survives.

### G2. Golden invariant tests that nobody weakens

A dedicated test file asserts the core invariants:
- Structural change during an active query = assertion failure.
- A tag component with non-zero size = static_assert at registration.
- A query outliving its `World` = assertion failure.
- Granting `Write<T>` access to a chunk bumps that chunk's column-version counter for T (conservative: bump happens on access, not on actual row mutation).
- Command buffer flush order matches record order.
- Lifecycle hooks attempting structural mutation (directly or via command buffer) = assertion failure.
- Component registration after the first entity is created = assertion failure.

These tests do not get weakened or skipped. If a change requires loosening one, that is a design change that requires a `decisions.md` entry.

### G3. No new core features without a design note

If during implementation the agent feels the need to add "shared components," "system groups," "observer events," "query predicate lambdas," or any feature not specified in this document, the agent **stops and writes a design note** explaining why the existing primitives are insufficient. The answer is almost always that they are sufficient and the feature is unneeded. This is how core stays small.

---

## What the Implementing Agent Does First

1. Read this document in full.
2. Read the existing ECS surface: `engine/include/world/`, `engine/include/core/batch/`, `engine/include/render/StaticMeshComponentStore.h`, `engine/src/world/transform/`, `engine/src/render/RenderExtractionSystem.cpp`. Understand what is being replaced.
3. Start Phase 0. Build the spike in `ecs_spike/` at the repo root. Do not touch the engine tree.
4. Run the Phase 0 benchmark. Record the result in `docs/ecs/decisions.md`.
5. If the exit criteria are met, proceed to Phase 1. If not, halt and surface findings before proceeding.

The agent has leeway within each phase to make implementation decisions — data-structure choices for the archetype graph, the specific shape of the chunk memory layout, the exact form of the query cache invalidation, whether `TransformHierarchyService` survives or is absorbed, whether cached render components are introduced. These are judgment calls.

The agent does **not** have leeway on the design axioms (A1–A8) or the pitfall mitigations (P1–P14). Those are the architectural spine. A design decision that violates one of them is a wrong decision regardless of how much faster or shorter it might be.
