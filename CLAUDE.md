# Sencha — Code Quality Constraints

Context for Claude Code working in the Sencha engine codebase. These are invariants, not suggestions. When a requested change conflicts with one of them, stop and say so before writing anything (see "When to push back"). Where this document has gone stale, the existing codebase is the source of truth.

## Prime directives

**1. Name mechanisms, never intents.** Types and modules are named for what they mechanically do, not for the gameplay outcome they happen to serve. `WorldPartitionRuntime`, not `MetroidvaniaZoneManager`. `IZonePopulationStrategy`, not `SurvivalHorrorSpawner`. The genre (Metroidvania, Zelda-like, survival horror) is a *configuration* of shape-neutral systems, never a vocabulary baked into the type system. If you find yourself reaching for a genre word, a project name (Loss Function, SINR, Tulpa), or an "intent" in an identifier, that is the signal to stop and find the neutral mechanism underneath.

**2. Requirements are user-facing, not engineering directives.** A feature request describes what a player or designer should be able to do. It is not an instruction for how to structure code. "Zones should stream as the player backtracks" is a requirement; the engineering answer is the existing WorldPartitionRuntime plus budgets and manifest data, not a new type that encodes "backtracking." Translate requests into the existing substrate plus data. Do not import the request's nouns into the architecture.

**3. Behavior comes from data, not branches.** New gameplay and content variation enter through data: manifests, `.smat`/`.stex`, cvars, gameplay tags, config, component values. It does not enter through hardcoded special-case branches or per-use-case code paths. The fidelity-tier system was deleted for exactly this reason: one pipeline, features toggled by data, no parallel pipelines per scenario. Hold that line everywhere.

**4. Earn every abstraction.** A seam (interface, strategy, virtual) exists because there is a real second implementation or a real axis of variation that exists *today*, not because one might appear. Sencha rejected an authored NavPath primitive in favor of area classification plus cost biasing, and greenlit exactly one irreducible abstraction (the hierarchical cross-zone planner) because it was genuinely irreducible. Default to the concrete thing. If you want an interface, justify the variation axis first. Consolidation and deletion are wins, not regressions.

These four pull against each other on purpose. Be modular and SOLID (directives 1 through 3) without manufacturing enterprise abstraction (directive 4).

## Naming

- Plain, mechanical names. The tea theme (Kettle, Teapot, Infuser, Essenchal) is retired. Current names are Engine, ServiceHost, Renderer, World, Registry, ZoneRuntime, FrameDriver. Match that register.
- No `Manager`, `Helper`, `Util`, `Handler` grab-bags. If a type needs one of those words to describe it, it is doing too many things. Split it.
- IDs are strongly typed via `StrongId<T>` (e.g. `StrongId<GameplayTagId>`). Do not pass a raw `uint32` or index where a strong id exists.
- No cute, no clever, no genre words, no project codenames in engine identifiers.

## Layering and dependencies

- Dependency direction flows one way. Engine / ServiceHost / FrameDriver host the frame; Renderer, World, Registry, ZoneRuntime sit under them. Lower layers do not reference higher ones.
- `World` owns `Registry` owns archetype storage; `ZoneRuntime` sits over the partition. Respect this containment; do not reach across it.
- The editors are separate executables with their own Registry: `kyusu` (level editor), `chakin` (material editor), and `kettle` (project launcher), all over the `editor_common` shell library. Product names stay on executables and window titles only; internal types are mechanically named. Editor-only and cook-only code never links into the runtime. Cook paths stay gated behind `SENCHA_ENABLE_COOK` and are dev-only.

## ECS rules

- Components are data. SoA chunks are 16KB. Components carry no behavior; logic lives in systems. No methods on components beyond trivial accessors.
- Structural changes (add or remove a component, create or destroy an entity) during iteration go through `CommandBuffer` only. Never mutate archetype membership inside a running query.
- Entity ids are generational. Never store or compare a raw index. Respect generation checks; a stale handle must fail the check, not silently alias a recycled slot.
- Change detection is `Changed<T>` and is chunk-conservative. Do not assume per-entity change granularity; if you write to a component in a chunk, treat the whole chunk as changed.
- Use cached `Query` objects. Do not rebuild a query every frame.
- Lifecycle work goes through `ComponentTraits` hooks, not ad-hoc init scattered across systems.
- Zero-size markers are tag components. Use them; do not fake a tag with a bool component.

## Concurrency rules

Two lanes, no third.

- **JobSystem / ThreadPoolJobSystem**: intra-frame fork-join. The caller participates. No nesting (a job does not spawn-and-wait on the same system). `worker_count == 0` is the deterministic single-thread path and must stay behaviorally identical to the parallel path.
- **AsyncTaskQueue**: cross-frame work. Results commit at `FramePhase::DrainAsyncTasks`, never mid-frame.
- Isolation is by disjoint registries, not locks. Parallelism comes from partitioning data so two workers touch disjoint state. Do not reach for a mutex to make shared mutation "safe." If two systems contend, the answer is usually a registry split or a phase boundary, not a lock.
- Do not spawn raw threads, `std::async`, or your own pools. Use the two lanes.
- Chunk-parallel queries (Stage D) are deferred behind a roughly 1ms profile gate. Do not parallelize a query speculatively. Measure first; if it is under the gate, it stays serial.

## Data-driven and configuration

- Tunables are cvars, surfaced through the dev console. New tunable behavior gets a cvar, not a recompile-to-change constant.
- Gameplay state and queries use the tag system in `core/gameplay_tags`. Tags are dotted names interned to registration-order `uint32` ids (id 0 is the sentinel; ids are runtime values, not hashes, so never serialize a tag id as if it were stable across builds). Membership is `GameplayTagSet`; refcounted grants are `CountedGameplayTagSet` with `GameplayTagSource` tracking; queries are `GameplayTagQuery` (All/Any/None, Exact or Hierarchical). Resolve hierarchy query-side via `IsDescendantOf` (inclusive). Do not invent a parallel string-keyed flag system alongside this.
- Assets flow through the `IAssetLoader` staged-load contract and the content-hashed cooked cache. New asset types implement the contract; they do not bolt on a side-channel loader. Material and texture data is `.smat`/`.stex`. Mesh data splits `StaticMesh`/`SkinnedMesh` over shared `MeshGeometry`; skinning streams stay separate from base vertex streams.

## SOLID, applied to this codebase

- **SRP**: one mechanical responsibility per type. The grab-bag-name test above is the smell.
- **OCP**: extend by adding a component, a system, or an implementation behind an existing seam (`IAssetLoader`, `IZonePopulationStrategy`, `IPoseModifier`, `IEditorCommand`). Do not extend by editing a central switch. If there is no seam and the variation is real and present, add one (directive 4), then extend through it.
- **LSP**: implementations honor the full contract of their interface, including ordering and lifecycle guarantees (for example the staged-load contract's phases). A loader that loads everything eagerly is not a valid `IAssetLoader`.
- **ISP**: narrow interfaces. The existing seams are deliberately small. Keep new ones small. No fat "do everything" interface.
- **DIP**: systems depend on the seam, not the concrete loader, strategy, or modifier. Concrete selection happens at composition, driven by data where possible.

## Determinism

- Default to determinism. The serial path (`worker_count == 0`, lazy deterministic schedule evaluation) is the reference; the parallel path must match it where it matters.
- Watch the usual nondeterminism sources: iteration order over unordered containers, time- or address-seeded randomness, float reduction order that differs between serial and parallel. If a change can diverge serial vs parallel, that is a defect, not a tuning detail.

## Testing and change hygiene

- The suite is green (roughly 852 tests at last count). Keep it green. New mechanisms ship with tests for the mechanism, not for the gameplay intent layered on top of it.
- Prefer the smallest change that satisfies the requirement. A net line reduction that preserves capability is a good outcome, not a missed opportunity to add structure.
- Do not leave dead seams, half-wired strategies, or "for future use" interfaces. If it is not used now, it does not exist yet.

## Comments and style

- Comments explain why, not what. The code says what it does. If a comment restates the line below it, delete the comment.
- No em dashes anywhere (code, comments, docs, commit messages). Use periods, colons, or parentheses.
- No filler, no marketing voice, no "elegant" or "robust" or "powerful" self-description in comments. State the constraint or the reason and stop.

## When to push back

Disagree before complying, not after. Stop and raise the conflict (do not silently write the code) when a request would:

- put a genre word, project name, or intent into an identifier or type (directive 1),
- add a special-case branch or parallel code path where data would do (directive 3),
- introduce an interface or strategy with no present second implementation (directive 4),
- add a lock, a raw thread, or a third concurrency lane,
- mutate archetype membership mid-iteration outside a CommandBuffer,
- link editor or cook code into the runtime,
- or break determinism between the serial and parallel paths.

State the conflict, name the invariant, propose the shape-neutral alternative. If the override stands after that, proceed. The point is that any contamination is a decision on the record, not an accident.
