# Sencha: Code Quality Constraints

Context for Claude Code working in the Sencha engine codebase. These are invariants, not suggestions. When a requested change conflicts with one of them, stop and say so before writing anything (see "When to push back"). The existing codebase is the source of truth for current behavior where this document has gone stale. It is not proof that the current shape is ideal; see "Escalating bad contracts."

## Prime directives

**1. Name mechanisms, never intents.** Types and modules are named for what they mechanically do, not for the gameplay outcome they happen to serve. `WorldPartitionRuntime`, not `MetroidvaniaZoneManager`. `IZonePopulationStrategy`, not `SurvivalHorrorSpawner`. The genre (Metroidvania, Zelda-like, survival horror) is a *configuration* of shape-neutral systems, never a vocabulary baked into the type system. If you find yourself reaching for a genre word, a project name (Loss Function, SINR, Tulpa), or an "intent" in an identifier, that is the signal to stop and find the neutral mechanism underneath.

**2. Requirements are user-facing, not engineering directives.** A feature request describes what a player or designer should be able to do. It is not an instruction for how to structure code. "Zones should stream as the player backtracks" is a requirement; the engineering answer is the existing WorldPartitionRuntime plus budgets and manifest data, not a new type that encodes "backtracking." Translate requests into the existing substrate plus data. Do not import the request's nouns into the architecture.

**3. Behavior comes from data, not branches.** New gameplay and content variation enter through data: manifests, `.smat`/`.stex`, cvars, gameplay tags, config, component values. It does not enter through hardcoded special-case branches or per-use-case code paths. The fidelity-tier system was deleted for exactly this reason: one pipeline, features toggled by data, no parallel pipelines per scenario. Hold that line everywhere. This is about special-casing core behavior; a small closed local switch is not a violation (see "Behavioral variation and dispatch").

**4. Earn every abstraction.** Default to concrete types inside a layer. A seam (interface, strategy, trait, extension point) is justified by a real boundary or a real axis of variation, not by symmetry, habit, or the possibility that variation might appear someday. Real boundaries that justify a seam even without a second shipping implementation:

- a game binary boundary (games extend the engine here),
- an editor boundary,
- an asset pipeline boundary,
- a scripting boundary,
- a test boundary (the mechanism cannot otherwise be tested without booting unrelated systems),
- a module boundary,
- a renderer or platform boundary,
- a data-selected runtime extension point,
- a real algorithmic variation axis that exists today.

A seam is invalid when it is an interface around exactly one class with no boundary behind it, a factory with no selection decision, a strategy with no real selection point, or an abstraction that makes the current code harder to read than the concrete version. Sencha rejected an authored NavPath primitive in favor of area classification plus cost biasing, and greenlit exactly one irreducible abstraction (the hierarchical cross-zone planner) because it was genuinely irreducible.

The full rule: default to concrete types inside a layer; use narrow seams at real boundaries; collapse fake boundaries; narrow bloated real boundaries; refactor responsibilities before adding another layer of indirection. Do not preserve a bad seam merely because code already depends on it, and do not hide a bad seam behind more adapters. If a seam makes every consumer uglier, propose changing the seam (see "Escalating bad contracts"). Consolidation and deletion are wins, not regressions.

These four pull against each other on purpose. Be decoupled, testable, and extendable (directives 1 through 3) without manufacturing enterprise abstraction (directive 4).

## Naming

- Plain, mechanical names. The tea theme (Kettle, Teapot, Infuser, Essenchal) is retired. Current names are Engine, ServiceHost, Renderer, World, Registry, ZoneRuntime, FrameDriver. Match that register.
- No `Manager`, `Helper`, `Util`, `Handler` grab-bags. If a type needs one of those words to describe it, it is doing too many things. Split it.
- IDs are strongly typed via `StrongId<T>` (e.g. `StrongId<GameplayTagId>`). Do not pass a raw `uint32` or index where a strong id exists.
- No cute, no clever, no genre words, no project codenames in engine identifiers.
- Do not use the word "polymorphic" as a recommendation. Say what you actually mean: "dispatch," "variation mechanism," "runtime seam," "compile-time policy," or "registered operation." Sencha is C++20 and intentionally light on inheritance; behavioral variation is not a request for an inheritance hierarchy.

## Layering and dependencies

- Dependency direction flows one way. Engine / ServiceHost / FrameDriver host the frame; Renderer, World, Registry, ZoneRuntime sit under them. Lower layers do not reference higher ones.
- `World` owns `Registry` owns archetype storage; `ZoneRuntime` sits over the partition. Respect this containment; do not reach across it.
- The editor (`sencha_editor`) is a separate executable with its own Registry. Editor-only and cook-only code never links into the runtime. Cook paths stay gated behind `SENCHA_ENABLE_COOK` and are dev-only.

## Files and translation units

A file groups one tight mechanism, not a whole subsystem vocabulary. Broad category files are junk drawers and are not acceptable.

Bad shapes: `MovementSystems.cpp` holding every movement-related system, `EditorCommands.cpp` holding every editor operation, `GameplaySystems.cpp`, `Systems.cpp`, `Helpers.cpp`, `Utils.cpp`, `RegistryStuff.cpp`.

Preferred shapes:

- one primary type per `.cpp`, with its private helpers beside it,
- a small tightly coupled family that always changes together,
- a registration file that only wires existing implementations together (wiring, no logic),
- a test file organized around one mechanism or contract.

Rules of thumb:

- If adding a type makes a file harder to scan, create a new file.
- If a file needs section banners to stay readable, split it.
- If multiple types in a file do not share private helpers, invariants, or lifecycle, they probably do not belong together.

Split by mechanism. Do not solve file explosion by making junk drawers, and do not solve junk drawers by making one-file-per-function noise.

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

## Behavioral variation and dispatch

Small switches are fine. Large behavior hubs are a smell. The line is what the branch controls and whether it will grow.

A small `switch` is acceptable for: serialization tags, tiny format distinctions, debug draw modes, mapping an enum to a string, and other small closed local choices.

A `switch` is a smell when it controls: core behavior, lifecycle, policy, editor operations, asset loading, movement behavior, gameplay rules, or anything likely to accumulate cases.

For core behavior, prefer, in roughly this order of reach:

- components plus systems, when behavior varies by entity state,
- data tables, tags, asset metadata, manifests, or cvars, when behavior should be content-authored,
- C++20 concepts or traits, when compile-time variation is clearer than runtime branching,
- function tables or registered operations, for small closed dispatch,
- command objects, when operations need identity, undo, redo, serialization, keybinds, or editor registration,
- separate named systems, when behavior has its own state, tests, lifecycle, or invariants,
- narrow runtime seams, only at real binary, module, editor, asset, renderer, or scripting boundaries.

Do not build a god function around `switch(mode)` and keep adding cases. Once a branch starts accumulating state, helper functions, special lifecycle rules, or cross-system knowledge, split the behavior into a named mechanism.

Do not replace branching with worse indirection either. Each mechanism has an entry bar:

- A runtime interface is justified only at a real boundary or a real variation axis.
- A concept is justified only when there are real model types or the constraint improves readability.
- A trait is justified when the variation is mechanical and compile-time.
- A command object is justified when the operation needs identity or tooling support.

Sencha is intentionally light on inheritance. Prefer values, components, free functions, concrete systems, traits, concepts, registries, and composition. Use virtual dispatch sparingly, mostly at module boundaries where runtime substitution is actually part of the design.

## SOLID, applied to this codebase

SOLID is a pressure test, not a religion. The goals it protects here are concrete:

- code can be tested without booting the whole engine,
- game binaries can extend the engine through intentional seams,
- lower layers do not know about higher layers,
- systems remain small and replaceable,
- data selects behavior where possible,
- deleting an abstraction is a valid architectural improvement.

Prefer plain values, free functions, concrete types, narrow interfaces, registries, traits, concepts, and composition roots. Forbidden regardless of which principle is invoked to justify them: interface soup, factory/provider/adapter stacks, abstract base classes by default, service locators, dependency-injection theater, and inheritance hierarchies created only to avoid a switch.

- **SRP**: one mechanical responsibility per type. The grab-bag-name test above is the smell.
- **OCP**: extend by adding a component, a system, a command, a loader, a modifier, a registered operation, a trait specialization, a concept model, or an implementation behind an existing narrow seam (`IAssetLoader`, `IZonePopulationStrategy`, `IPoseModifier`, `IEditorCommand`). Do not extend core behavior by growing a central `switch`, a mode enum, a branch pile, or a registry of special cases. If behavior has identity, lifecycle, tests, or policy, give it a named mechanism. If there is no seam and the variation is real and present, add the narrowest seam that fits (directive 4), then extend through it. If the existing seam is the wrong shape, propose changing the seam instead of making consumers worse (see "Escalating bad contracts").
- **LSP**: implementations honor the full contract of their interface, including ordering and lifecycle guarantees (for example the staged-load contract's phases). A loader that loads everything eagerly is not a valid `IAssetLoader`.
- **ISP**: narrow interfaces. The existing seams are deliberately small. Keep new ones small. No fat "do everything" interface.
- **DIP**: systems depend on the seam, not the concrete loader, strategy, or modifier. Concrete selection happens at composition, driven by data where possible. DIP does not mean every dependency gets an interface; it means real seams point the right direction.

## Escalating bad contracts

Do not blindly work around awkward architecture. If implementing a feature cleanly is blocked by a lower-level contract, and obeying that contract would produce ugly consumer code (excessive adapters, repeated boilerplate, unnatural ownership flow, duplicated state, central branching, knowledge leaking across layers), stop and identify whether the lower-level seam is the real problem. Say so before writing the workaround.

Expected behavior when this happens:

1. Explain the consumer code that would result from obeying the existing contract.
2. Identify the contract, seam, ownership rule, data shape, or API causing the ugliness.
3. Propose the smaller architectural refactor that would make the high-level implementation cleaner.
4. Explain what code becomes simpler after the refactor.
5. Ask before performing the architectural rewrite, unless architecture cleanup was explicitly requested.

Escalation triggers:

- A caller must know too much about another layer's internals.
- A high-level feature requires repeated setup or teardown boilerplate.
- A mode enum or switch in consumer code is compensating for a missing mechanism.
- A supposedly generic API forces game-specific branching.
- A seam is too narrow, too wide, or pointed in the wrong direction.
- The clean implementation wants data-driven selection, but the lower layer only exposes hardcoded branches.
- The consumer has to duplicate state already owned by a lower layer.
- Test code is difficult because the contract requires booting unrelated systems.
- Multiple consumers are forming the same awkward adapter around a bad API.
- Ownership is ambiguous: it is unclear which layer, type, registry, runtime object, or system owns the state or lifecycle.
- Adding a small feature requires touching too many classes, especially across multiple layers.
- A change that should be local forces edits in unrelated systems, registries, factories, command wiring, serializers, or editor glue.
- Multiple classes exist mostly to forward calls, translate shapes, or route around an awkward contract.
- Wrapper types are accumulating around another type because the underlying API is not shaped for its real consumers.
- The consumer has to assemble too many low-level pieces manually before it can express the actual high-level operation.
- A class exists mainly to compensate for another class having the wrong responsibility.
- A new feature requires parallel state because the natural owner does not expose the right operation.
- The code keeps adding adapter or bridge shapes without reducing complexity at the call site.
- The most direct implementation is obvious, but the current ownership model makes it illegal, awkward, or leaky.

When these appear, do not solve the problem by adding another wrapper. Identify the bad ownership boundary, contract, or responsibility split, and propose the smallest refactor that makes the consumer code natural. A good architecture makes common high-level operations easy to express; if every consumer performs the same ritual before using a system, the ritual belongs behind a better API.

This is not permission for rewrites by default. Escalate only when the lower-level shape is actively forcing bad consumer code. The preferred response is not "I worked around it." The preferred response is "this contract is making the consumer worse; here is the smaller refactor that would remove the friction."

## Determinism

- Default to determinism. The serial path (`worker_count == 0`, lazy deterministic schedule evaluation) is the reference; the parallel path must match it where it matters.
- Watch the usual nondeterminism sources: iteration order over unordered containers, time- or address-seeded randomness, float reduction order that differs between serial and parallel. If a change can diverge serial vs parallel, that is a defect, not a tuning detail.

## Testing and change hygiene

- The suite is green (roughly 852 tests at last count). Keep it green. New mechanisms ship with tests for the mechanism, not for the gameplay intent layered on top of it.
- Prefer the smallest change that satisfies the requirement. A net line reduction that preserves capability is a good outcome, not a missed opportunity to add structure.

### Unused code: classify before recommending deletion

"Not wired up yet" is not by itself grounds for deletion in an engine. Before recommending removal of unused or partially wired code, classify it:

- **Planned infrastructure**: anchored to a declared Sencha engine capability, has a clear future consumer, protects a real boundary, and can be tested on its own. Keep it. Do not recommend deleting it merely because the final consumer is not wired yet.
- **Speculative abstraction**: exists only because something might someday need variation. No declared capability, no identified consumer, no boundary. Remove it.
- **Stale plan**: the code or a comment explicitly marks future engine work, but the plan may no longer hold. Question it before deleting; ask whether the capability is still intended rather than silently removing it.
- **Dead seam**: no longer protects a boundary, no longer has a consumer, or only makes callers worse. Remove it. Do not preserve it because code already depends on it; migrate the callers and delete it.

Do not leave new half-wired strategies or "for future use" interfaces behind your own changes: if you add a mechanism, either wire it to its consumer or anchor it explicitly to the declared capability it serves.

## Comments and style

- Comments explain why, not what. The code says what it does. If a comment restates the line below it, delete the comment.
- No em dashes anywhere (code, comments, docs, commit messages). Use periods, colons, or parentheses.
- No filler, no marketing voice, no "elegant" or "robust" or "powerful" self-description in comments. State the constraint or the reason and stop.

## When to push back

Disagree before complying, not after. Stop and raise the conflict (do not silently write the code) when a request would:

- put a genre word, project name, or intent into an identifier or type (directive 1),
- add a special-case branch or parallel code path where data would do (directive 3),
- introduce a seam with no real boundary or present variation axis behind it (directive 4),
- grow a central behavioral `switch`, mode enum, or branch pile instead of a named mechanism,
- add types to a junk-drawer file instead of splitting by mechanism,
- delete planned infrastructure that is anchored to a declared engine capability,
- add a lock, a raw thread, or a third concurrency lane,
- mutate archetype membership mid-iteration outside a CommandBuffer,
- link editor or cook code into the runtime,
- or break determinism between the serial and parallel paths.

State the conflict, name the invariant, propose the shape-neutral alternative. If the override stands after that, proceed. The point is that any contamination is a decision on the record, not an accident.
