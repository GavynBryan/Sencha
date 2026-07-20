# Sencha Engineering Constitution

This file is the authoritative repository-level guidance for every coding agent working in Sencha. Read it completely before planning, reviewing, or editing. These are engineering invariants, not suggestions. A short request changes the amount of explanation required, not the standard of engineering required.

## Instruction precedence and evidence

Use this precedence when sources disagree:

1. The user request defines the desired product outcome and explicitly approved scope.
2. This file defines repository engineering constraints. When a request conflicts with one, name the conflict before proceeding.
3. The current working tree and its tests define existing behavior and contracts.
4. Current architecture documentation explains intended ownership and dependency direction.
5. Roadmaps and plans describe intended work. They are not proof that a mechanism has landed.
6. Comments, commit messages, examples, and historical documents are supporting evidence only.

Never create, extend, or depend on a type merely because a plan, example, or document names it. Search the current tree first. Capitalized prose names are not proof that a type exists or should exist.

When documentation and code disagree about current status, verify behavior in source and tests. Correct directly relevant stale documentation as part of the change. The current shape is evidence of behavior, not proof that the shape is ideal. See "Escalating bad contracts."

## Required start-of-task protocol

Before planning, reviewing, or editing:

1. Read this file completely.
2. Run `git status --short` and inspect relevant existing diffs. Treat all pre-existing modifications as user-owned.
3. Locate and read the owning header, implementation, tests, registration or composition code, and directly relevant architecture documentation.
4. Search for all producers, consumers, sibling implementations, and duplicated versions of the operation.
5. State the invariant being changed in terms independent of a particular screen, callback, or gameplay scenario.
6. Identify the layer and object that own the invariant.
7. Trace every applicable part of the vertical path:
   - authoring document or schema,
   - editor command and interaction transaction,
   - import or cook path,
   - persisted or cooked representation,
   - loader, registry, and cache,
   - ECS component and system,
   - schedule or frame phase,
   - render extraction and backend,
   - diagnostics, tests, and fitness functions.
8. Identify compatibility, ABI, threading, ownership, and hot-path consequences before choosing the implementation shape.

Do not edit after reading only one consumer. Do not assume the first matching class owns the concept. Planning requests receive the same repository inspection and architectural care as implementation requests.

## Prime directives

**1. Name mechanisms, never intents.** Types and modules are named for what they mechanically do, not for the gameplay outcome they happen to serve. `WorldPartitionRuntime`, not `MetroidvaniaZoneManager`. `PopulationPolicy`, not `SurvivalHorrorSpawner`. The genre is a configuration of shape-neutral systems, never vocabulary baked into the type system. If you reach for a genre word, project name, or gameplay intent in an identifier, stop and find the neutral mechanism underneath.

**2. Requirements are user-facing, not engineering directives.** A feature request describes what a player or designer should be able to do. It does not prescribe code structure. "Zones should stream as the player backtracks" is a requirement. The engineering answer should use the existing partition, budget, and manifest substrate rather than introduce a type that encodes backtracking.

**3. Behavior comes from data, not branches.** New gameplay and content variation enter through manifests, assets, cvars, gameplay tags, config, component values, schemas, and registered data. They do not enter through hardcoded use-case branches or parallel pipelines. A small closed local switch is acceptable. A central behavior switch that grows with features is not.

**4. Earn every abstraction.** Default to concrete types inside a layer. A seam such as an interface, strategy, trait, or extension point is justified by a real boundary or a real variation axis, not by symmetry, habit, or hypothetical future use.

Real boundaries that can justify a seam even before a second shipping implementation include:

- a game binary boundary,
- an editor boundary,
- an asset pipeline boundary,
- a scripting boundary,
- a module boundary,
- a renderer or platform boundary,
- a data-selected runtime extension point,
- a test boundary when the mechanism cannot otherwise be tested without unrelated systems,
- a real algorithmic variation axis that exists today.

A seam is invalid when it is an interface around one class with no boundary behind it, a factory with no selection decision, a strategy with no selection point, or an abstraction that makes the current code harder to read than the concrete version.

Default to concrete types inside a layer. Use narrow seams at real boundaries. Collapse fake boundaries. Narrow bloated real boundaries. Refactor responsibilities before adding another layer of indirection. Do not preserve a bad seam because callers already depend on it, and do not hide it behind more adapters.

These directives pull against each other on purpose. Be decoupled, testable, and extendable without manufacturing enterprise abstraction.

## Naming

- Use plain mechanical names. The tea theme is retired for internal engine types. Product names remain acceptable for executables and window titles.
- Do not create `Manager`, `Helper`, `Util`, or `Handler` grab-bags. If one of those words is needed to describe a type, inspect whether it owns too many responsibilities.
- Use existing strong ID types. Do not pass a raw integer or array index where a strong ID exists.
- Do not store or compare raw entity indices. Entity identity is generational.
- Do not use cute names, genre words, project codenames, or other engines as identifiers or behavior descriptions.
- Do not recommend "polymorphism" as a shape. Name the actual mechanism: dispatch, runtime seam, compile-time policy, registered operation, trait, command, or data table.

## Layering, ownership, and dependencies

Dependency direction flows toward stable lower-level mechanisms. Lower layers do not reach upward into hosts, editors, or game-specific code.

- `Engine` is the integration root for services, frame hosting, scheduling, timing, and worker lanes.
- `Registry` wraps ECS `World` storage plus registry-local resources.
- `ZoneRuntime` owns the global registry and loaded zone registries.
- `FrameDriver` owns the outer frame pipeline.
- Render extraction copies simulation state into render-domain data. Graphics backends consume extracted data, not live ECS state.
- Editor executables own their editor registries and authoring state. Editor-only and cook-only code never links into the shipping runtime.
- Cook paths stay gated behind `SENCHA_ENABLE_COOK` and remain dev-only.
- Jolt stays behind the physics firewall. Vulkan and SDL details stay behind their intended boundaries.
- There is no service locator and no general dependency container. Ownership and dependencies are explicit.

Do not reach across ownership boundaries for convenience. If a caller repeatedly assembles another layer's internals, improve the owning API.

## Files and translation units

A file groups one tight mechanism, not a subsystem vocabulary.

Bad shapes include:

- `MovementSystems.cpp` containing every movement system,
- `EditorCommands.cpp` containing every editor operation,
- `GameplaySystems.cpp`,
- `Systems.cpp`,
- `Helpers.cpp`,
- `Utils.cpp`,
- `RegistryStuff.cpp`.

Preferred shapes include:

- one primary type per `.cpp`, with private helpers beside it,
- a small tightly coupled family that always changes together,
- a registration file that only wires existing implementations together,
- a test file organized around one mechanism or contract.

Split by mechanism. Do not solve file growth with junk drawers. Do not solve junk drawers with one-file-per-function noise. If multiple types do not share private helpers, invariants, or lifecycle, they probably do not belong together.

## ECS rules

- Components are data. Components carry no behavior beyond trivial accessors.
- Archetype chunks use SoA storage in 16 KB blocks.
- Register component types before the first entity is created in a `World`.
- Structural changes during a query or lifecycle hook go through `CommandBuffer`. Never mutate archetype membership inside active iteration.
- Lifecycle work belongs in `ComponentTraits` hooks, not scattered ad-hoc initialization.
- Lifecycle hooks may retain or release external handles, but must not perform structural ECS mutation.
- Zero-size markers are tag components. Do not fake a tag with a bool component.
- `Changed<T>` is chunk-conservative. A write to one entity can mark the component column for the whole chunk.
- Non-const access counts as a write. Use const access for pure reads.
- Use cached `Query` objects. Do not rebuild a query every frame.
- Do not cache chunk row pointers across structural changes unless invalidated through structural-version tracking.
- Do not store owning runtime resources directly in relocatable component storage. Prefer values, handles, IDs, and registry resources.
- Systems must tolerate zero, one, or many matching entities.

## Concurrency rules

Two lanes, no third.

- **JobSystem / ThreadPoolJobSystem**: intra-frame fork-join. The caller participates. Jobs do not spawn and wait on the same pool. `worker_count == 0` is the deterministic serial reference path.
- **AsyncTaskQueue**: cross-frame work. Results commit at `FramePhase::DrainAsyncTasks`, never mid-frame.
- Parallel isolation comes from disjoint registries or disjoint data partitions, not shared mutation hidden behind locks.
- Do not spawn raw threads, call `std::async`, or create another pool.
- Do not add a mutex as the first answer to ownership contention. Prefer a registry split, data partition, or phase boundary.
- Do not parallelize a query speculatively. Measure first. The existing profile gate is roughly 1 ms.
- Owner-thread resources remain owner-thread resources. Async staging may prepare plain CPU data, but publication, cache mutation, and GPU work commit on the owner thread.

## Determinism

- Default to determinism. The serial path is the reference. Parallel execution must match it where the product contract requires equivalent results.
- Watch unordered-container iteration, time-seeded or address-seeded randomness, floating-point reduction order, task completion order, and unstable registration order.
- A serial versus parallel divergence is a defect, not a tuning detail.
- Determinism claims require evidence from both paths when the change can affect scheduling or ordering.

## Data-driven configuration

- Tunables are cvars exposed through the dev console. New tunable behavior gets a cvar rather than a recompile-to-change constant.
- Gameplay state and queries use `core/gameplay_tags`. Do not invent a parallel string-keyed flag system.
- Gameplay tag IDs are registration-order runtime values, not stable hashes. Never serialize them as stable identities.
- Assets flow through the staged `IAssetLoader` contract and content-hashed cooked cache. New asset types implement that contract rather than adding a side-channel loader.
- Runtime formats are cooked formats. Source importers remain in the dev-only cook layer.
- Stable authoring identity belongs in documents and assets. Dense runtime indices and tables belong in compiled runtime data.
- Repeated interpretation, ID resolution, schema traversal, and parsing should be moved out of hot paths.

## Behavioral variation and dispatch

Small switches are fine. Large behavior hubs are a smell. The distinction is what the branch controls and whether it will grow.

A small `switch` is acceptable for serialization tags, tiny format distinctions, debug draw modes, enum-to-string mapping, and other closed local choices.

A `switch` is a smell when it controls core behavior, lifecycle, policy, editor operations, asset loading, movement behavior, gameplay rules, or anything likely to accumulate cases.

For core behavior, prefer in roughly this order:

- components plus systems when behavior varies by entity state,
- data tables, tags, asset metadata, manifests, schemas, or cvars when behavior is authored,
- C++20 concepts or traits when compile-time variation is clearer,
- function tables or registered operations for small closed dispatch,
- command objects when operations need identity, undo, redo, serialization, keybinds, or editor registration,
- separate named systems when behavior owns state, tests, lifecycle, or invariants,
- narrow runtime seams only at real binary, module, editor, asset, renderer, platform, or scripting boundaries.

Do not replace a branch with worse indirection. Each mechanism has an entry bar. A runtime interface requires a real boundary or variation axis. A trait requires mechanical compile-time variation. A command requires operation identity or tooling semantics.

Sencha is intentionally light on inheritance. Prefer values, components, free functions, concrete systems, traits, concepts, registries, commands, and composition roots. Use virtual dispatch sparingly at boundaries where runtime substitution is part of the design.

## SOLID, applied to Sencha

SOLID is a pressure test, not a religion. Its useful goals here are concrete:

- code can be tested without booting the whole engine,
- game binaries extend the engine through intentional seams,
- lower layers do not know about higher layers,
- systems remain small and replaceable,
- data selects behavior where possible,
- deleting an abstraction is a valid improvement.

Prefer plain values, free functions, concrete types, narrow interfaces, registries, traits, concepts, and composition roots. Forbidden regardless of the principle used to justify them: interface soup, factory-provider-adapter stacks, abstract base classes by default, service locators, dependency-injection theater, and inheritance hierarchies created only to avoid a switch.

- **SRP**: one mechanical responsibility per type.
- **OCP**: extend through a component, system, command, loader, registered operation, trait specialization, concept model, or an existing proven seam. If a real variation exists and no seam fits, add the narrowest earned seam.
- **LSP**: implementations honor the full contract, including ordering, ownership, and lifecycle guarantees.
- **ISP**: interfaces remain narrow. Do not create a fat interface to avoid passing explicit dependencies.
- **DIP**: real boundaries point in the correct direction. It does not mean every dependency receives an interface.

## Escalating bad contracts

Do not blindly work around awkward architecture. If a lower-level contract would force excessive adapters, repeated boilerplate, unnatural ownership, duplicated state, central branching, or cross-layer knowledge, identify the contract as the likely problem before adding another wrapper.

Expected response:

1. Explain the consumer code the current contract would force.
2. Identify the contract, ownership rule, data shape, or API causing the ugliness.
3. Propose the smallest architectural refactor that makes the high-level implementation natural.
4. Explain which callers and invariants become simpler.

A requested implementation includes permission for the smallest local, behavior-preserving prerequisite refactor required to satisfy this file.

Escalate before editing when the cleaner solution would:

- materially expand product scope,
- change a public SDK or module ABI contract,
- change or migrate a persisted or cooked format,
- delete a declared capability,
- replace a major subsystem,
- require destructive data conversion,
- or choose between materially different product semantics.

Do not request permission merely to extract duplicated logic, move an invariant to its correct owner, narrow an unhealthy private API, or delete a workaround made obsolete by the requested change.

Bad-contract triggers include:

- a caller must know another layer's internals,
- repeated setup or teardown boilerplate,
- a mode enum compensating for a missing mechanism,
- a generic API forcing game-specific branching,
- duplicated state already owned elsewhere,
- tests requiring unrelated systems to boot,
- multiple consumers forming the same adapter,
- ambiguous ownership or lifecycle,
- a local feature forcing unrelated edits across many layers,
- forwarding-only wrappers accumulating around a bad API,
- a class existing mainly to compensate for another class's responsibility,
- bridge shapes increasing without reducing call-site complexity.

The preferred response is not "I worked around it." The preferred response is "this contract is making the consumer worse; here is the smallest correction."

## State, lifecycle, and editor interaction

State has one owner.

- Persisted authoring state belongs to the document and changes through undoable commands.
- Cross-panel editor state belongs to the editor workspace or service that owns it.
- Drag previews and pointer-local state stay local unless another surface genuinely consumes them.
- Runtime simulation state belongs to runtime systems and registries.
- Derived values are computed or cached with explicit invalidation. Do not synchronize duplicate canonical state.
- Live edits follow begin, preview, commit or cancel. Every interruption path must terminate the transaction.
- Escape, focus loss, tool switching, document closing, and shutdown must not strand preview state.
- Input plumbing passes complete events or complete domain event values. Do not decompose and silently drop modifiers or other fields.
- Interaction math should use plain engine or editor-domain types where possible so it can be tested without GUI or graphics startup.

## Performance requirements

Optimize architectural shape first. Micro-optimize only with evidence.

In per-frame or per-entity hot paths, avoid:

- authoring-document traversal,
- schema lookup or parsing,
- repeated string splitting, regex, or JSON work,
- rebuilding maps, sets, arrays, closures, or temporary objects,
- broad registry scans when a cached query or capability table can make work proportional to matches,
- GPU resource creation or cache mutation from worker threads,
- backend traversal of live ECS state.

Prefer:

- compiled tables and stable indices,
- cached queries and precomputed bindings,
- retained objects updated in place,
- bounded queues and explicit budgets,
- reused scratch storage when profiling proves allocation pressure,
- work proportional to active systems, active assets, visible registries, and matching components.

When changing a hot path, state expected complexity. If the change plausibly affects frame time or allocation rate, add a targeted diagnostic, benchmark, or before-and-after measurement. Do not claim performance improvement without evidence.

## Sencha change-path checklists

Check only paths applicable to the requested change, but make the decision explicitly.

### ECS or component changes

Check:

- component registration before entity creation,
- `ComponentTraits` lifecycle behavior,
- structural mutation and `CommandBuffer` boundaries,
- `TypeSchema`, `ComponentManifest`, serializers, and `ComponentStorageTraits`,
- const versus write access and chunk-conservative change detection,
- generational identity and strong IDs,
- cached query behavior,
- registry and zone participation,
- serial and parallel determinism,
- focused ECS and runtime tests.

### Asset changes

Check:

- source import and cook behavior,
- cooked extension and cache identity,
- `AssetRegistry` discovery,
- staged load versus owner-thread commit,
- runtime cache retain and release,
- dependency and preload ordering,
- hot reload and invalidation,
- editor selection and preview,
- diagnostics for missing or invalid assets.

Do not introduce a second loading path for convenience.

### Editor interaction changes

Check:

- persisted document state versus transient interaction state,
- `CommandStack` ownership,
- begin, preview, commit, and cancel behavior,
- escape, focus loss, tool switching, and shutdown,
- complete input-event propagation,
- multi-selection and stable identity,
- GUI-independent math extraction where headless testing is possible,
- undo and redo regression coverage.

### Runtime, rendering, or graphics changes

Check:

- fixed simulation time versus presentation wall time,
- frame-phase ownership,
- simulation-to-render extraction,
- prohibition on backend traversal of live ECS state,
- owner-thread GPU resource creation and destruction,
- cache and handle lifecycle,
- expected CPU and GPU complexity,
- allocation behavior in repeated paths,
- zero-resource and many-resource behavior,
- device loss, resize, and teardown paths when applicable.

### World partition changes

Check:

- global registry versus zone registries,
- authored identity versus runtime identity,
- zone participation in visible, physics, logic, and audio views,
- detached async construction and main-thread attach,
- streaming budgets and commit boundaries,
- topology, adjacency, and diagnostics,
- dormant-zone behavior,
- deterministic scheduling and unload lifecycle.

### Public SDK or module-boundary changes

Check:

- whether the type is reachable through installed headers or exported symbols,
- layout, calling convention, compiler, standard library, and build configuration consequences,
- ABI fingerprint consequences,
- host and game-module skew,
- `sizeof` and `offsetof` coverage where applicable,
- module isolation and ABI fitness checks,
- whether a POD descriptor or data contract can replace a new virtual.

### Persisted or cooked format changes

Check:

- current version contract,
- backward-compatible load behavior,
- deterministic serialization,
- missing and unknown value preservation,
- migration input, output, failure, and rollback behavior,
- runtime and editor agreement,
- test fixtures for old and new data.

Do not bump a format version for an editor-only improvement. Do not silently discard unresolved values or substitute defaults that change meaning.

## Testing standards

Tests protect invariants and externally meaningful behavior.

- Put regression coverage at the layer that owns the invariant.
- Pure model, geometry, parser, and resolver code gets focused table-driven tests.
- ECS tests cover structural safety, lifecycle, identity, queries, and deterministic behavior.
- Asset tests cover stage, commit, identity, dependency ordering, and lifetime.
- Editor tests cover commands, cancellation, selection, event completeness, undo, and redo.
- Runtime tests cover frame phases, module boundaries, streaming handoff, and deterministic paths.
- Performance-sensitive changes need a representative measurement or a test that bounds work.
- A regression test must fail before the fix for the intended reason.
- Do not use timing sleeps when deterministic state or events can prove behavior.
- Never delete, skip, loosen, or snapshot over a failing test merely to make a change pass.

### Unused code: classify before recommending deletion

"Not wired up yet" is not enough to declare code dead. Classify it:

- **Planned infrastructure**: anchored to a declared capability, protects a real boundary, has a clear future consumer, and can be tested independently. Keep it.
- **Speculative abstraction**: exists only because something might someday need variation. Remove it.
- **Stale plan**: future intent is recorded but may no longer hold. Verify before deleting.
- **Dead seam**: no longer protects a boundary, has no consumer, or makes callers worse. Migrate callers and remove it.

Do not leave new half-wired strategies or future-use interfaces. Wire the mechanism to its consumer or anchor it explicitly to a declared capability with tests.

## Evidence before claims

- Do not call code unused based only on missing direct call sites. Check registration, reflection, CMake inclusion, asset discovery, module loading, editor commands, data-driven lookup, and tests.
- Do not call a path hot without identifying how often it runs.
- Do not claim a performance improvement without representative measurement.
- Do not claim determinism without comparing serial and parallel paths where relevant.
- Do not claim compatibility without identifying the persisted, cooked, SDK, or ABI contract.
- Do not claim a bug fixed without a reproduction, regression test, or explicit reason neither is feasible.
- Do not describe a subsystem as absent or complete based only on a roadmap status paragraph.
- Do not claim a test or command passed unless it was actually run.

## Working-tree discipline

- Existing modifications belong to the user unless proven otherwise.
- Read relevant diffs before editing a modified file.
- Preserve unrelated changes, formatting, naming, and organization.
- Do not run destructive Git commands.
- Do not reset, clean, stash, checkout, amend, commit, or push unless explicitly requested.
- Do not run repository-wide formatting for a local change.
- Do not update generated, vendored, or cooked output unless the repository intentionally tracks it and the change requires regeneration.
- Keep the diff scoped to requested behavior and the smallest architectural support required.
- Necessary prerequisite refactoring is allowed. Opportunistic cleanup is not.

## Required verification

For a normal code change, use the canonical preset workflow:

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
git diff --check
```

Run focused tests first while iterating, for example:

```sh
ctest --test-dir build -R '<relevant-pattern>' --output-on-failure
```

or run the relevant GoogleTest executable with a focused filter.

Do not run CTest in parallel by default. CI intentionally runs the suite serially. Parallel CTest requires evidence that affected tests and temporary resources are isolated.

Additional verification:

- Concurrency changes: exercise `worker_count == 0` and the parallel path. Run the `tsan` preset when supported and relevant.
- Public or module-boundary changes: run module ABI, layout, and isolation coverage.
- Editor layering changes: run editor and mesh-edit dependency fitness tests.
- Physics changes: preserve the Jolt firewall and run physics isolation.
- Runtime performance changes: record scenario, baseline, result, and measurement method.
- Interaction changes: repeat the actual pointer, keyboard, focus, cancellation, and undo path.
- Bug fixes: repeat the original reproduction.
- Documentation-only changes: run `git diff --check` and verify referenced paths, names, links, and commands. The application suite is not required unless executable examples, scripts, generated files, or developer tooling changed.

If a required command cannot be run, report the change as unverified or incomplete. State exactly what was not run and why.

## Comments and style

- Comments explain why, not what.
- Write for a reader with no session context.
- Type comments state purpose and invariants. Variable comments state non-obvious meaning or units. Other comments record genuine ordering, ownership, lifecycle, or workaround constraints.
- Do not reference other engines, games, project plans, task numbers, conversation phases, or requests as behavior descriptions.
- Do not document absences unless warning about a real trap.
- Do not editorialize about replaced code.
- Comments and documentation should sound like they were written by a maintainer who understands the code, not an agent narrating a patch. Use normal punctuation and natural sentence rhythm. Be direct, specific, and proportionate to the thing being explained.
- Avoid canned framing, repetitive restatement, excessive headings, rhetorical flourish, mirrored contrast constructions, marketing adjectives, self-congratulation, and change-log narration embedded in source comments.

## When to push back

Disagree before complying, not after. Stop and name the conflict when a request would:

- put genre, project, or gameplay intent into an engine identifier,
- add a special-case branch or parallel pipeline where data should select behavior,
- introduce a seam with no real boundary or variation axis,
- grow a central behavioral switch, mode enum, or branch pile,
- add types to a junk-drawer file,
- delete planned infrastructure anchored to a declared capability,
- add a lock, raw thread, `std::async`, or a third concurrency lane,
- mutate archetype membership during active iteration outside `CommandBuffer`,
- link editor or cook code into the shipping runtime,
- let a graphics backend traverse live ECS state,
- change a public ABI or persisted format without explicit treatment,
- or break required serial and parallel equivalence.

State the conflict, name the invariant, and propose the shape-neutral alternative. If the user explicitly overrides after the conflict is named, proceed and record the tradeoff honestly.

## Definition of done

A change is complete only when all applicable statements are true:

- The invariant has one identifiable owner.
- The implementation uses an existing mechanism or introduces the smallest earned mechanism.
- Consumers do not duplicate resolution, conversion, lifecycle, or policy logic.
- No game, genre, project, or feature intent leaked into engine vocabulary.
- ECS structural, lifecycle, storage, and identity rules remain valid.
- Serial and parallel behavior remain equivalent where required.
- Async work publishes only through the declared owner-thread boundary.
- Editor, cook, runtime, render, and graphics dependencies still point correctly.
- Public ABI and persisted formats are unchanged or explicitly handled.
- Hot paths have not gained unbounded work, repeated parsing, or avoidable allocation.
- Focused regressions protect the owning invariant.
- Focused tests, required build, complete suite, and `git diff --check` pass when applicable.
- The original feature or bug path has been exercised.
- Remaining diagnostics and unrun verification are reported honestly.

Do not describe a partial implementation as finished.

## Required handoff

Every implementation handoff reports:

1. The invariant and owning mechanism.
2. The material files or subsystems changed.
3. Compatibility, ABI, threading, and performance consequences.
4. Focused verification performed.
5. Full verification performed.
6. Anything not run, unresolved, or intentionally deferred.

Do not list every edited line. Report the architectural shape and the evidence that it works.
