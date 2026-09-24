# Sencha Engineering Constitution

This is the repository guidance for every coding agent working in Sencha. Read all of it before planning, reviewing, or editing. A short request changes how much you explain. It does not change the engineering standard.

Everything here serves one goal: building games on Sencha should be easy. That takes behavior selected by data, one owner for every invariant, one implementation of every mechanism, and code a maintainer can read without a guide. When two rules seem to pull against each other, choose the reading that serves that goal and say which way you went.

## Sources of truth

When sources disagree, use this order.

1. The user request, together with the plan document for the task when one exists, defines the product outcome and approved scope. Review findings against a plan are binding and get dispositioned by number.
2. This file defines engineering constraints. If a request conflicts with it, name the conflict before proceeding.
3. The working tree and its tests define existing behavior and contracts.
4. Architecture and subsystem documentation define intended ownership and dependency direction.
5. Roadmaps and subsystem plans describe future work. They do not prove a mechanism exists. They do tell you which consumers are coming, and you should use them for exactly that when shaping an API.
6. Comments, commit messages, and historical documents are supporting evidence only.

Never assume a type exists because a document names it; search the tree. When documentation and code disagree about current behavior, verify in source and tests and fix the directly relevant documentation in the same change.

Deviating from a plan or handoff instruction is sometimes the right call. Deviating silently is never acceptable. Name it and give the reason.

## Start of task

1. Read this file and `docs/deferred.md`. If the task hits a trigger recorded there, that work is part of the task.
2. Run `git status --short` and read relevant diffs. Pre-existing modifications belong to the user.
3. Read the owning header, implementation, tests, registration code, subsystem documentation, and the subsystem's plan if one exists.
4. Search for producers, consumers, sibling implementations, and existing versions of the operation. Search `core/` and the backend for general mechanisms the change should reuse (see "Reuse before writing").
5. State the invariant being changed in terms independent of any particular screen, callback, or gameplay scenario, and identify the layer and object that own it.
6. Trace every applicable stage of the vertical path: authoring schema, editor command and transaction, import or cook, cooked representation, loader and cache, ECS component and system, frame phase, render extraction and backend, diagnostics and tests.
7. Identify compatibility, ABI, threading, ownership, and hot-path consequences before choosing a shape.

Don't edit after reading one consumer, and don't assume the first matching class owns the concept. Planning requests get the same inspection as implementation requests.

## Prime directives

**Name mechanisms, never intents.** Types and modules are named for what they mechanically do. `WorldPartitionRuntime`, not `MetroidvaniaZoneManager`. `PopulationPolicy`, not `SurvivalHorrorSpawner`. A genre is a configuration of shape-neutral systems. If you reach for a genre word, project name, or gameplay intent in an identifier, find the mechanism underneath it.

**Requirements are user-facing.** A request describes what a player or designer should be able to do. It does not prescribe code structure. "Zones should stream as the player backtracks" is answered with the existing partition, budget, and manifest substrate, not a type that encodes backtracking.

**Behavior comes from data.** Gameplay and content variation enter through manifests, assets, cvars, gameplay tags, schemas, component values, and registered data. A small closed local switch is fine. A central switch that grows with features is not.

**One mechanism, one implementation.** If Sencha already does something, use it. If it nearly does, extend it. Two implementations of the same mechanism are a defect regardless of which came first.

**Shape APIs for the consumers you know about.** Real boundaries, real variation, and known upcoming consumers justify structure. Imagined consumers do not. The next section covers how to weigh the two ways of getting this wrong.

## Abstraction and duplication

This repository has two ways to get structure wrong: indirection nobody needs, and the same mechanism written twice. Both happen. The second has cost more, because an agent that defers cleanup does not remember it next session, and the naive version stays in the tree until someone finds it by hand. Weigh them with that in mind.

**Extracting shared code is not speculative abstraction.** A function, type, or template that two call sites need is deduplication. It needs no further justification, and it happens at the second occurrence. If your change would add code that does what existing code already does, reuse the existing code, extend it, or extract the common part now. That work is in scope for the change that would otherwise create the duplicate.

**A seam is justified by a boundary, a variation axis, or a known consumer.** A seam here means indirection: a virtual interface, a runtime-selected strategy, a registered operation table, an extension point, a concept written to admit several models. Any one of these justifies one:

- a game-binary, editor, asset-pipeline, scripting, module, renderer, or platform boundary,
- a data-selected runtime extension point,
- a test boundary, when the mechanism can't otherwise be tested without unrelated systems,
- a variation axis that exists in the tree today,
- a known consumer, meaning one named in the current ticket, the subsystem's active plan, or its design documentation. It doesn't need to exist yet.

**Design for the next consumers the plan names.** Before settling an API, read the plan for the subsystem and list the consumers it says are coming. Shape the code so those consumers add something (a component, a system, a registered operation, a trait specialization, a data entry) rather than edit what you are writing. If a known consumer would have to reopen and modify your code to fit, the seam is due now.

**What still doesn't earn a seam:** an interface around one class with no boundary behind it and no known second consumer, a factory with no selection decision, a strategy with no selection point, a wrapper that only forwards, service locators, dependency containers, abstract base classes by default, inheritance built to avoid a switch, or indirection that makes the current code harder to read with no known payoff. Collapse fake boundaries and narrow bloated ones. Refactor responsibilities before adding a layer.

**No silent deferral.** The default is to do the work now. Defer only when the next consumer's design is genuinely undecided, or when doing it now would change a public ABI or persisted format, which needs escalation anyway. When you defer, add an entry to `docs/deferred.md` recording what was deferred, where the code lives, the concrete trigger that makes it due, and the consumer that will trigger it. Report the entry in the handoff. When a trigger lands, do the work and delete the entry. Your own memory is not a ledger.

## Reuse before writing

Before writing anything general-purpose, find the existing implementation. The categories most often duplicated are binary serialization and byte streams, file and path handling, hashing and content identity, string parsing and formatting, containers and handle pools, strong IDs, math and geometry, spatial queries, logging and diagnostics, and job or async dispatch.

Search `core/` and the backend by what the code does, not by the name you would have given it. Grep for the verbs (serialize, write, read, encode, hash) as well as type names. Known shared mechanisms include the binary serializer in `core`, `GameplayTagRegistry` and `GameplayTagQuery`, cvars and the dev console, `CommandBuffer`, `CommandStack`, `JobSystem` and `AsyncTaskQueue`, `IAssetLoader` and `AssetRegistry`, and `TypeSchema` and `ComponentManifest`.

If you still write something new and general, put it in the layer where the next consumer will find it, not beside its first caller, and say in the handoff what you searched and why nothing fit.

## Computation and IO are separate responsibilities

Code that transforms data does not decide where the data goes. Cookers, importers, builders, bakers, and compilers take values in and return values out, or write into a serializer the caller supplies. They do not open files, build output paths, create directories, name cache entries, or decide when to flush. The cook pipeline owns destinations, naming, content-hashed cache identity, and write atomicity. Loaders stay inside the staged `IAssetLoader` contract and add no file access of their own. Editor commands mutate documents; the document's save path writes them.

The canonical violation is a navmesh cooker that builds the mesh, encodes it with a hand-written binary writer, and saves the file. That is three responsibilities and one duplicated mechanism. The correct shape is a builder that returns navmesh data, a serialize function built on the engine serializer, and registration with the cook pipeline that persists the result. Every piece is then testable in memory with no temporary files.

## Naming

Use plain mechanical names. The tea theme is retired for internal engine types; product names are fine for executables and window titles. Avoid `Manager`, `Helper`, `Util`, and `Handler`, since needing one of those words usually means the type owns too much. Use the existing strong ID types rather than raw integers or indices, and never store or compare raw entity indices, because entity identity is generational. No genre words, project codenames, or other engines in identifiers. When describing a design, name the actual mechanism (dispatch, trait, registered operation, command, data table) instead of "polymorphism."

## Layering and ownership

Dependencies point toward stable lower-level mechanisms. Lower layers never reach into hosts, editors, or game code.

`Engine` is the integration root for services, frame hosting, scheduling, timing, and worker lanes. `Registry` wraps ECS `World` storage plus registry-local resources. `RuntimeWorld` owns the single partitioned entity `World`; zone residency maps `ZoneId` to storage partitions, and partition zero is persistent. `FrameDriver` owns the outer frame pipeline. Render extraction copies simulation state into render-domain data, and graphics backends consume only extracted data. Editor executables own their registries and authoring state, and editor-only or cook-only code never links into the shipping runtime. Cook paths stay behind `SENCHA_ENABLE_COOK`. Jolt stays behind the physics firewall, and Vulkan and SDL stay behind their boundaries. There is no service locator and no general dependency container.

If a caller keeps assembling another layer's internals, improve the owning API instead.

## Files

A file holds one tight mechanism: one primary type with its private helpers, a small family that always changes together, a registration file that only wires existing pieces, or tests for one contract. `MovementSystems.cpp`, `EditorCommands.cpp`, `Systems.cpp`, and `Utils.cpp` are junk drawers. One file per function is the opposite mistake. If types don't share helpers, invariants, or lifecycle, they don't share a file.

## ECS

- Components are data, with at most trivial accessors.
- Archetype chunks are 16 KB SoA blocks.
- Register component types before the first entity is created in a `World`.
- Structural changes during a query or lifecycle hook go through `CommandBuffer`.
- Lifecycle work lives in `ComponentTraits` hooks. Hooks may retain or release external handles but never mutate ECS structure.
- Zero-size markers are tag components, never bool components.
- `Changed<T>` is chunk-conservative, and non-const access counts as a write. Use const access for reads.
- Cache `Query` objects. Don't rebuild them per frame.
- Don't cache chunk row pointers across structural changes without structural-version tracking.
- Don't store owning runtime resources in relocatable component storage. Use values, handles, IDs, and registry resources.
- Systems handle zero, one, or many matches.

## Concurrency and determinism

There are two lanes. `JobSystem` does intra-frame fork-join; the caller participates, jobs never spawn and wait on the same pool, and `worker_count == 0` is the serial reference path. `AsyncTaskQueue` does cross-frame work whose results commit at `FramePhase::DrainAsyncTasks`. No raw threads, no `std::async`, no third pool.

Parallel isolation comes from disjoint registries or data partitions. A mutex is not the first answer to contention; look for a registry split, partition, or phase boundary first. Owner-thread resources stay on the owner thread: async work may prepare plain CPU data, but publication, cache mutation, and GPU work commit on the owner. Don't parallelize a query without measuring; the profile gate is about 1 ms.

The serial path is the reference, and a serial versus parallel divergence is a defect. Watch unordered iteration, time- or address-seeded randomness, floating-point reduction order, task completion order, and registration order. A determinism claim needs evidence from both paths.

## Data-driven configuration

Tunables are cvars. Gameplay state and queries use `core/gameplay_tags`; tag IDs are registration-order runtime values and are never serialized as stable identity. Assets go through the staged `IAssetLoader` contract and the content-hashed cooked cache, with no side-channel loaders. Runtime formats are cooked formats, and source importers stay in the dev-only cook layer. Stable authoring identity lives in documents and assets; dense runtime indices live in compiled data. Move interpretation, ID resolution, and parsing out of hot paths.

## Dispatch

A small closed `switch` is fine for serialization tags, format distinctions, debug modes, and enum-to-string. A switch that controls core behavior, lifecycle, policy, editor operations, asset loading, or gameplay rules, or that will keep gaining cases, is a missing mechanism. In rough order of preference: components and systems when behavior follows entity state; data (tables, tags, manifests, schemas, cvars) when behavior is authored; concepts or traits for compile-time variation; function tables or registered operations for small closed dispatch; commands when operations need identity, undo, serialization, or keybinds; separate systems when behavior owns state and lifecycle; runtime seams at real boundaries. Sencha is light on inheritance by design.

## SOLID

SOLID is a pressure test. Here it means code tests without booting the engine, games extend the engine through intentional seams, and lower layers don't know about higher ones. Each type has one mechanical responsibility, and reading, transforming, and writing are three. Extension happens by adding a component, system, command, loader, registered operation, or trait specialization. Implementations honor ordering, ownership, and lifecycle contracts. Interfaces stay narrow. Dependency inversion applies at real boundaries, not to every dependency. Deleting an abstraction that isn't paying for itself is a valid improvement.

## Escalating bad contracts

Don't work around awkward architecture with another wrapper. If a lower-level contract would force adapters, repeated boilerplate, unnatural ownership, duplicated state, central branching, or cross-layer knowledge, say so: describe the consumer code the contract forces, name the contract causing it, propose the smallest correction, and explain which callers get simpler.

A requested implementation includes permission for the smallest local, behavior-preserving prerequisite refactor. That covers extracting duplicated logic, moving an invariant to its owner, narrowing an unhealthy private API, and deleting a workaround the change makes obsolete. Escalate before editing when the cleaner solution would expand product scope, change a public SDK or module ABI, change or migrate a persisted or cooked format, delete a declared capability, replace a major subsystem, require destructive data conversion, or choose between different product semantics.

## Editor state and interaction

Every piece of state has one owner. Persisted authoring state belongs to the document and changes through undoable commands. Cross-panel state belongs to the workspace or service that owns it. Drag previews and pointer-local state stay local unless something else consumes them. Derived values are computed or cached with explicit invalidation, never synchronized as duplicate canonical state.

Live edits follow begin, preview, commit or cancel, and every interruption (escape, focus loss, tool switch, document close, shutdown) terminates the transaction. Input plumbing passes complete events; don't decompose them and drop modifiers. Interaction math uses plain engine or editor types so it tests without GUI or graphics startup.

## Performance

Get the architectural shape right first and micro-optimize only with evidence. Per-frame and per-entity paths don't traverse authoring documents, parse schemas or strings, rebuild containers or closures, scan registries broadly when a cached query would do, create GPU resources from workers, or let a backend walk live ECS state. Prefer compiled tables, cached queries, precomputed bindings, retained objects, bounded queues, and work proportional to what's active. When you change a hot path, state the expected complexity, and measure if frame time or allocation rate could move. Don't claim an improvement without a measurement.

## Change-path checklists

Check the paths your change touches and state which ones apply.

**ECS or components:** registration order, `ComponentTraits` lifecycle, `CommandBuffer` boundaries, `TypeSchema`, `ComponentManifest`, serializers, `ComponentStorageTraits`, const versus write access, generational identity, cached queries, registry and zone participation, serial and parallel equivalence.

**Assets:** import and cook, cooked extension and cache identity, `AssetRegistry` discovery, staged load versus owner-thread commit, cache retain and release, dependency and preload order, hot reload and invalidation, editor preview, diagnostics for missing or invalid assets. One loading path only.

**Editor interaction:** document state versus transient state, `CommandStack` ownership, begin/preview/commit/cancel, every interruption path, complete input events, multi-selection and stable identity, headless-testable math, undo and redo coverage.

**Runtime and rendering:** fixed simulation time versus presentation time, frame-phase ownership, extraction, no live ECS in backends, owner-thread GPU lifetime, handle lifecycle, CPU and GPU complexity, allocation in repeated paths, zero and many resources, device loss, resize, and teardown.

**World partition:** global versus zone registries, authored versus runtime identity, zone participation in visibility, physics, logic, and audio, detached async construction with main-thread attach, streaming budgets, topology and adjacency, dormant zones, deterministic unload.

**Public SDK or module boundary:** reachability through installed headers or exports, layout and calling convention, ABI fingerprint, host and module skew, `sizeof` and `offsetof` coverage, isolation checks, and whether a POD descriptor can replace a new virtual.

**Persisted or cooked formats:** version contract, backward-compatible load, deterministic serialization, preservation of unknown values, migration failure and rollback, runtime and editor agreement, fixtures for old and new data. Don't bump a version for an editor-only improvement, and don't substitute defaults that change meaning.

## Comments and documentation

Names, types, and structure tell a reader what the code does. The documentation in `docs/` tells them why a subsystem is shaped the way it is. Comments cover only what neither can reach: an ordering, ownership, lifecycle, or threading constraint at the exact line where it applies, units a type doesn't carry, or a workaround with its reason and the condition for removing it.

Before writing a comment, work through these:

1. Rename the variable, function, or type until the comment is redundant.
2. Extract a named function or a named intermediate value.
3. Move the logic to its correct owner so the boundary explains it.
4. Encode the rule in the type system. A comment saying "must not be null," "call X before Y," or "only valid on the render thread" describes a missing strong type, private constructor, narrower API, concept constraint, `static_assert`, or debug assertion. Make misuse fail to compile or fail loudly.
5. If the explanation is design rationale or spans more than one function, write it in the subsystem doc and leave at most a one-line pointer.

Whatever survives that is one or two lines. Beyond that:

- No comment block longer than three lines in source.
- No doc comment on a type or function by default. Add one only when the contract can't be expressed in the signature, such as ownership transfer, thread affinity, or complexity guarantees on a public API.
- No restating the signature, no parameter-by-parameter narration, no section banners, no commented-out code, no change narration, and no references to tickets, conversations, plans, or other engines.

Documentation is part of the change. Read the subsystem doc before editing the subsystem, and update it when behavior, ownership, or a contract changes.

Before handoff, review your diff for comments. Wherever you wrote a long or verbose comment, replace it with documentation, clearer and more concise names, more legible code, or the proper abstraction boundary. If comment lines are more than roughly one in ten of the lines you added, treat that as a sign the pass isn't done.

## Testing

Tests protect invariants and meaningful behavior, and regressions go at the layer that owns the invariant. Pure model, geometry, parser, and resolver code gets focused table-driven tests. ECS tests cover structural safety, lifecycle, identity, queries, and determinism. Asset tests cover stage, commit, identity, dependency order, and lifetime. Editor tests cover commands, cancellation, selection, event completeness, undo, and redo. Runtime tests cover frame phases, module boundaries, streaming handoff, and deterministic paths. A regression test must fail before the fix for the intended reason. Use deterministic state or events instead of sleeps. Never delete, skip, loosen, or snapshot over a failing test to make a change pass.

Before recommending that unused code be deleted, classify it. Planned infrastructure anchored to a declared capability or known consumer stays. Speculative abstraction with no boundary and no known consumer goes. A stale plan gets verified before deletion. A dead seam gets its callers migrated and is removed. Check registration, reflection, CMake, asset discovery, module loading, editor commands, and data-driven lookup before calling anything unused. Structure you build for a known consumer is wired to its current consumer and tested, and the pending consumer is recorded in `docs/deferred.md` if the plan doesn't already name it.

## Evidence before claims

Don't call code unused from missing call sites alone, or a path hot without saying how often it runs. Don't claim a performance improvement without measurement, determinism without comparing serial and parallel paths, compatibility without naming the contract, a fix without a reproduction or regression test, or a passing command you didn't run. Don't describe a subsystem as absent or complete from a roadmap paragraph.

## Working tree

Existing modifications belong to the user. Read relevant diffs before editing a modified file and preserve unrelated changes, formatting, and organization. Don't reset, clean, stash, checkout, amend, commit, or push unless asked. Don't run repository-wide formatting for a local change or regenerate tracked output the change doesn't require. Unrelated cleanup is out of scope. Removing duplication that your change touches or would otherwise create is in scope.

## Verification

For a normal code change:

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
git diff --check
```

Iterate with focused tests first, for example `ctest --test-dir build -R '<pattern>' --output-on-failure` or a filtered GoogleTest binary. Run CTest serially unless the affected tests are proven isolated.

Concurrency changes run `worker_count == 0` and the parallel path, plus the `tsan` preset when relevant. Boundary changes run module ABI, layout, and isolation coverage. Editor layering changes run the editor and mesh-edit dependency fitness tests. Physics changes run physics isolation. Performance changes record scenario, baseline, result, and method. Interaction changes repeat the real pointer, keyboard, focus, cancel, and undo path. Bug fixes repeat the original reproduction. Documentation-only changes need `git diff --check` and a check that referenced paths, names, and commands are real.

If something required couldn't run, report the change as unverified and say what and why.

## Push back first

Stop and name the conflict before proceeding when a request would put genre, project, or gameplay intent into an engine identifier; add a branch or parallel pipeline where data should select behavior; write a second implementation of an existing mechanism; mix IO into a transform; add a seam with no boundary, variation axis, or known consumer; grow a central behavior switch or mode enum; add to a junk-drawer file; delete planned infrastructure; add a lock, raw thread, `std::async`, or third concurrency lane; mutate archetype membership outside `CommandBuffer` during iteration; link editor or cook code into the runtime; let a backend traverse live ECS state; change a public ABI or persisted format without explicit treatment; or break serial and parallel equivalence.

State the conflict, name the invariant, and propose the shape-neutral alternative. If the user overrides, proceed and record the tradeoff.

## Done and handoff

A change is done when the invariant has one owner; the implementation uses an existing mechanism or the smallest earned new one; nothing duplicates an existing mechanism; consumers don't duplicate resolution, conversion, lifecycle, or policy; transforms don't do IO; no game or genre intent leaked into engine vocabulary; ECS, concurrency, and layering rules hold; ABI and formats are unchanged or explicitly handled; hot paths gained no unbounded work; focused regressions protect the invariant; the comment pass is done; the required build and tests pass; and the original feature or bug path was exercised. A partial implementation is reported as partial.

The handoff reports:

1. The invariant and its owning mechanism.
2. The material files or subsystems changed.
3. Existing mechanisms reused, and any new general-purpose mechanism along with what you searched first.
4. Compatibility, ABI, threading, and performance consequences.
5. Entries added to or resolved in `docs/deferred.md`.
6. Focused and full verification performed.
7. Anything not run, unresolved, or deferred.

Report the architectural shape and the evidence, not every edited line.