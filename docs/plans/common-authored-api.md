# Common authored API: verbs

Status: implemented, 2026-09-20. Every stage below has landed: the catalog and
identity (`engine/include/authored/VerbId.h`, `VerbRegistry.h`), typed values
and bindings (`VerbArguments.h`, `VerbBinding.h`, `VerbBindingCompiler.h`,
`VerbBindingSet.h`, the `authored.bindings` subtype in `VerbBindingData.h`),
dispatch and tracing (`VerbDispatcher.h`, `VerbInvocation.h`, `VerbTrace.h`),
the runtime and editor lifecycle (`app/EngineVerbs.h`, `WorldVocabulary.h`,
Kyusu documents, Shoji's `VocabularyCatalog`), the shell proof
(`ui/UiVerbBindings.h`, `app/ShellVerbs.h`, `engine/assets/data/shell.bindings.sdata`),
and the relay proof (`logic/VerbRelay.h`, `VerbRelayBindingStore.h`,
`VerbRelaySystem.h`, the arena template's `arena.award_score`). Type names
below that were proposals are now the shipped names; the prose is kept as the
design record. Known limitation: a placed relay's binding key is persisted as
the key's sixteen-hex-digit hash, since a component cannot carry a string;
an authoring surface that spells the key resolves it through the binding
asset's records. This plan establishes the shared foundation
before Shoji behavior authoring, animation markers, level logic, flowcharts, and
the AbilityKit redesign acquire separate action vocabularies.

The invariant is:

> Every authored producer resolves and invokes the same semantic vocabulary in
> its World. The implementation retains responsibility for execution timing,
> authority, state ownership, and mutation safety.

The chosen shape is one World-scoped catalog, a separate runtime dispatch table,
and registered concrete operations. An operation may admit work into an existing
domain queue when its semantics require a boundary. There is no universal router,
mandatory subsystem inbox, process-global registry, or central object containing
the implementation of every verb.

**Scope and completion boundary**

The first complete delivery includes typed verb declarations, name resolution,
compiled bindings, checked dispatch, explicit lifetime rules, optional causality
tracing, authored UI bindings, stock Resume/Quit integration, and a small
scheduled relay proving a non-UI producer. Editors can discover and validate
vocabulary without starting gameplay or enabling application operations.

This work does not implement a visual behavior graph, generic queries, a generic
event bus, value endpoints, automatic RPC, prediction, animation markers, a full
trigger/relay/counter/timer level-I/O suite, or an AbilityKit redesign. Those are
later consumers. Normal internal C++ calls remain appropriate; native gameplay
does not have to invoke verbs to call an attribute or effect operation.

The acceptance bar is shared semantics, not a large initial verb library. Start
with `runtime.resume`, `application.quit`, and a game-defined operation exercised
by both UI and relay. Names are proposed public content contracts and must be
finalized with their behavior before the first fixtures ship. Do not register
placeholder `audio.play`, `entity.enable`, `entity.destroy`, or `effect.apply`
entries whose behavior, lifetime, or authority semantics are unspecified.

**Repository evidence**

| Existing mechanism | Verified behavior and consequence |
| --- | --- |
| [UiAction](../../engine/include/ui/UiAction.h), [UiScreenDesc](../../engine/include/ui/UiScreenDesc.h), [UiValue](../../engine/include/ui/UiValue.h) | Actions contain copied screen identity, a screen-local action ID, and owned presentation arguments. Action declarations currently contain names, not parameter signatures. Preserve this boundary. |
| [PauseMenu](../../engine/src/app/PauseMenu.cpp), [PauseMenuModel](../../engine/include/app/PauseMenuModel.h) | The shell drains a local activation action carrying a row index, maps it to a command, and dispatches a native callback. The model already supports stable command IDs and native customization. Authored integration must expose the row-to-operation relation rather than merely hide a verb call inside the callback. |
| [PauseState](../../engine/include/app/PauseState.h) | Pause transitions have an existing owner and phase rules. Live network sessions use input-only pause. A common verb must preserve these semantics. |
| [Game](../../engine/include/app/Game.h) | `OnRegisterVocabulary(World&)` is registration-only. The comment assigns runtime invocation to a game's `OnStart`; `Engine::Run` currently does not call it automatically. The repository search found the editor installer and module tests, not a production game override. |
| [Engine startup](../../engine/src/app/Engine.cpp) | Constructs `RuntimeWorld`, then content services, mounts/publishes content, creates the shell, calls `OnStart`, and later registers systems. Vocabulary installation must be inserted before content resolution; implementation binding can occur as concrete owners become ready. |
| [Kyusu module composition](../../editor/kyusu/src/app/EditorServices.cpp), [document construction](../../editor/kyusu/src/document/EditorDocument.cpp), [vocabulary tests](../../test/editor/ModuleVocabularyTests.cpp) | The game hook is replayed into new document Worlds. Existing documents are not retroactively updated. Workspace Worlds are destroyed before module unload. |
| [InputActionRegistry](../../engine/include/input/InputActionRegistry.h), [implementation](../../engine/src/input/InputActionRegistry.cpp), [tests](../../test/framework/InputActionResolveTests.cpp) | Names retain dense IDs through rebuilds; removed names retire their slots; returning names recover their slots; duplicate/empty names reject the rebuild atomically. This is an identity precedent, not a ready multi-provider registration mechanism. |
| [DataSchema](../../engine/include/core/metadata/DataSchema.h), [validation](../../engine/src/core/metadata/DataSchema.cpp) | Existing metadata covers most argument shapes. Reference validation does not resolve assets, subtypes, entities, or gameplay tags. There is no entity-reference field kind or runtime argument layout. |
| [DataAssetTypeRegistry](../../engine/include/assets/data/DataAssetTypeRegistry.h), [DataAssetLoader](../../engine/src/assets/data/DataAssetLoader.cpp), [DataAssetCache](../../engine/include/assets/data/DataAssetCache.h) | `.sdata` already supports schema validation, compilation, dependencies, cached typed values, and reload versions. Shared compiled assets cannot contain one World's verb IDs or subsystem pointers. |
| [GameContexts](../../engine/include/app/GameContexts.h), [EngineSchedule](../../engine/include/app/EngineSchedule.h), [frame phases](../../engine/src/app/EngineFramePhases.cpp) | Phase data is narrow; systems use concrete objects and erased trampolines. The scheduler does not automatically flush a universal command buffer. |
| [World](../../engine/include/ecs/World.h), [CommandBuffer](../../engine/include/ecs/CommandBuffer.h) | Structural mutation during active queries/lifecycle hooks is prohibited. A structural operation needs a concrete buffer owner and safe flush point. |
| [PersistentEntityIndex](../../engine/include/world/identity/PersistentEntityIndex.h), [identity types](../../engine/include/core/identity/Id.h) | Authored persistent identity resolves to generational `EntityId`. Runtime and current Kyusu document construction install the index; do not infer editor absence from older comments in its header. |
| [UiPackage](../../engine/include/assets/ui/UiPackage.h), [UiPreviewModel](../../editor/shoji/src/authoring/UiPreviewModel.h) | The package contains presentation resources; `.preview.json` contains sample model declarations/values. Neither is a runtime behavior asset. |
| [AnimationClip](../../engine/include/anim/AnimationClip.h), [playback](../../engine/src/anim/AnimationClipPlaybackSystem.cpp), [PhysicsWorld](../../engine/include/physics/PhysicsWorld.h) | Animation advances on fixed logic but has no marker representation. Physics has trigger-body flags, but its inspected public interface has no ready overlap-event stream. A small relay is the bounded second producer. |
| [ConsoleTypes](../../engine/include/core/console/ConsoleTypes.h), [OptionsPage](../../engine/src/app/OptionsPage.cpp), [player setting registration](../../engine/src/app/EngineConsoleBuiltins.cpp) | CVars have types, ranges, choices, validation and read/write policy. Options adds curated presentation metadata; display-mode choices currently live in a validator closure rather than `EnumValues`. |
| [AbilityKit registration](../../engine/src/abilities/AbilityKit.cpp) | Current registries, activation requests, and schedule ordering expose gameplay requirements. They are prototype architecture and impose no compatibility requirement on this plan. |

Architecture context is in [authored UI](../ui/architecture.md),
[input mapping](../gameplay/input.md), and the
[unified World plan](unified-runtime-world.md). Their status prose does not
override source. In particular, older UI and AbilityKit documents describe
states that have moved on. Update directly affected documentation when the
implementation lands, without treating this plan as proof of shipped behavior.

**Ownership and public coherence**

| Responsibility | Owner |
| --- | --- |
| API contract | Shared authored mechanisms: identity, schemas, binding, invocation, diagnostics |
| Vocabulary declaration | Engine or game code defining a verb's name and argument contract |
| Implementation | A concrete operation registered behind that contract |
| Runtime state | Existing services, scheduled systems, World resources, and application objects |
| Consumption | UI binding controllers, relay systems, later animation/flowchart/ability producers |

An individual verb has a useful declaration and implementation owner. That owner
does not establish a separate consumer-facing API. `audio.play` can be declared
and implemented by Audio while appearing in exactly the same catalog as a
game-defined verb. Prefixes organize authored names; dispatch must never parse
them to select a subsystem.

A new consumer depends on the common contract. A new verb adds its declaration
and concrete implementation binding without changing a central behavior switch.
A replacement implementation leaves consumers unchanged when the semantic
contract is unchanged. Hundreds of verbs grow table entries and domain-local
code, not a central class's members or dependencies.

One runtime entity World has one catalog. Its storage partitions do not each
have a vocabulary. Isolated editor documents, previews, and simulations have
independent catalogs. The host selects the World for a consumer at composition
time; invocation does not discover a World through a singleton.

**Mechanisms and dependency direction**

Proposed shared files belong under `engine/include/authored/` and
`engine/src/authored/`. Keep one mechanism or tightly coupled family per file:

| Proposed type/file family | Contract |
| --- | --- |
| `VerbId.h` | Strong runtime IDs and contract revision values; never persisted as authored identity |
| `VerbRegistry.h/.cpp` | Names, IDs, copied definitions, schemas, retirement, revision checks, deterministic enumeration |
| `VerbInvocation.h` | Invocation identity, optional parent identity, admission status, borrowed argument view |
| `VerbArguments.h/.cpp` | Owned typed values and indexed views for supported argument shapes |
| `VerbBinding.h` | World-independent binding description and World-bound compiled binding |
| `VerbBindingCompiler.h/.cpp` | Schema validation, argument-source mapping, constant compilation, contextual reference resolution |
| `VerbDispatcher.h/.cpp` | Implementation table, checked invocation, binding lifetime, optional trace sink |

These are proposed responsibilities, not a requirement to create empty files or
an interface for each row. Combine tightly coupled values where that improves
clarity. Do not create an `IAuthoredApi`, generic service container, or forwarding
facade over these concrete mechanisms.

The registry is a World resource. The dispatcher is one explicitly composed
runtime object associated with that catalog, owned by the runtime host and
passed to producers and implementation composition. Metadata Worlds require
only the registry. Do not publish every subsystem through World resources just
to make an implementation find its dependencies.

The shared layer depends on stable identity, schema, and plain value mechanisms.
It must not include `Engine`, concrete audio/physics/network services, RmlUi,
editor headers, or backend types. A contextual binding pass may use explicit
asset and entity resolution inputs; it must not acquire a general `Get<T>()`
dependency escape hatch. UI integration can depend on shared authored types;
shared authored types cannot depend on UI.

The registry stores metadata; the dispatcher stores executable bindings. A
combined registry would not inherently be a god object, but separating them
serves existing editor/runtime and object-lifetime boundaries. Editor discovery
must work without runtime services and must never enable Quit or Resume in the
editor application merely because a module declares those names.

**Vocabulary identity and registration**

Persist an exact, case-sensitive qualified name. Use a documented ASCII
identifier grammar for dot-separated segments, allow underscores, reject empty
segments and surrounding whitespace, and never silently normalize persisted
spelling. Names are semantic operations, such as `runtime.resume`, rather than
service accessors such as `GetWorld`.

Each definition contains the name, display name, description, and a record-root
argument schema. A category is optional presentation metadata. Do not add
speculative network, prediction, alias, or execution-policy matrices. Timing and
availability must be documented by the operation contract and observable through
admission diagnostics; callers do not choose a scheduling policy.

Registration rules:

- Slots are dense, one-based strong IDs, valid only within their catalog.
- A name retains its slot for that catalog's lifetime; removal retires it.
- A retired ID cannot call an implementation or acquire another name's meaning.
- A returning name may revive its slot, but compiled bindings still need the
  current contract revision.
- Conflicting declarations fail with a diagnostic naming the verb and provider.
  There is no first-wins or last-wins policy.
- Repeated installation is idempotent only for the same provider and identical
  contract. Compare schema structure, not callback addresses.
- A provider's registration batch is validated before publication. An invalid
  batch leaves the prior catalog unchanged. Use a concrete registration scope
  around composition; do not introduce another game-module registration API.
- Catalog mutation occurs only at startup or an explicit owner-thread rebuild
  boundary with producers stopped. It is forbidden during dispatch.
- Enumeration is deterministic. Do not expose unordered-map order to tools,
  diagnostics, serialization, or tests.

The current game hook returns `void`. The registration scope must retain errors
and let the host fail vocabulary installation after the hook returns, even when
game code ignores an individual registration return value. This provides a
checked installation without adding a virtual method or changing its signature.

Contract revision changes when argument meaning/layout changes. Presentation
label changes do not require rebinding. An implementation replacement also has
a binding generation, separate from schema revision. Compiled bindings carry a
catalog-lifetime identity plus the verb ID and revision. A pointer alone is not
a sufficient lifetime identity because an allocator can reuse its address.
Runtime-only identity counters are diagnostic/safety state, never simulation
ordering keys or serialized IDs.

Do not cache pointers into registry vectors. Resolve to strong IDs and validate
the relevant generations. Catalog changes are rare; O(number of definitions)
work at installation is acceptable. Runtime dispatch does not rescan the catalog.

**Argument schemas and representations**

Reuse `DataFieldSchema` as the authoritative description. Do not create parallel
verb-specific definitions for ranges, enum choices, descriptions, arrays, or
optionals. Implement a runtime value representation because UI values and JSON
do not represent the full typed contract; that representation is not a second
schema language.

The initial supported family is bool, checked integer, finite floating point,
string, enum, vector, record, array, optional, asset reference, data-asset
reference, gameplay tag, and the entity reference needed by the relay proof.
If a shape cannot yet be compiled, reject it at declaration/binding time rather
than accepting metadata that invocation cannot honor. Complex collection support
can land within the argument stage before any verb declaring it is exposed.

Add entity-reference metadata to the common schema language at the narrowest
point that serves both binding validation and tools. Use canonical persistent
identity strings in authored files, not JSON numbers or runtime entity indices.
Do not claim arbitrary cross-prefab-instance addressing is solved by a persistent
ID. V1 supports identified entities in the bound World and explicit runtime
entity inputs; richer instance-relative references need their own demonstrated
consumer.

Separate validation responsibilities:

| Boundary | Validation |
| --- | --- |
| Declaration | Well-formed schema, unique field keys, supported shapes, valid defaults/constraints |
| Asset load | Envelope/version, binding structure, explicit input kinds, constant syntax |
| World binding | Verb exists, exact argument coverage, types, ranges, tag names, asset kind/subtype and reference policy |
| Invocation | Dynamic value kinds/ranges, catalog and binding generations, implementation availability |
| Deferred execution | Entity generation/liveness, current authority, residency/partition participation, retained asset validity |

Do not rely on `ValidateDataAgainstSchema` to insert defaults or resolve
references. Define default insertion once in the binding compiler. Distinguish a
missing field from an explicit null. Preserve existing `Required` and `Optional`
semantics and test their combinations. Range `Step` is presentation metadata
unless the operation explicitly requires quantization; never silently snap a
gameplay argument because a widget offered a step.

`JsonValue` stores numbers as doubles. Check integer range and exactness before
conversion, and reject values that cannot be represented safely. Persist 64-bit
identities using their existing string codecs. Typed invocation must not turn
strings such as `"3"` into integers opportunistically. Explicit UI conversion,
where needed, is compiled at the binding boundary and reports failure.

The runtime argument pack uses indexed typed slots and owned backing storage for
variable-sized data. The binding compiler calculates field order, conversions,
and static validation once. Native adapters read typed slots through checked
accessors; do not build a universal C++ struct-reflection system for v1.

Constants are immutable compiled data. Invocation borrows a view only for the
duration of dispatch. A deferred implementation copies required values or retains
an immutable owning argument block and the needed asset leases. It never retains
references into `UiAction`, scratch buffers, a schema vector, or an editor DOM.
Resource acquisition and release stay with existing asset owners.

**Binding assets and load stages**

Introduce a proposed `authored.bindings` `.sdata` subtype through the existing
`DataAssetTypeRegistry`, `DataSchemaRegistry`, and staged asset loader. Add its
registration beside existing engine data subtype composition, not through a
filesystem reader inside a controller.

A binding has a stable authored key, verb name, named arguments, and declared
input slots. Each argument is either a typed constant or an explicit input-slot
reference. V1 has no expression evaluator, arbitrary property path, service
reference, or implicit string interpolation.

For illustration, a binding named `apply_to_target` can name a game verb and map
its `Target` argument from an entity input while supplying a numeric constant.
The same binding shape works for a UI controller and a relay; producer-to-input
mapping is outside the verb definition.

Compile in three steps:

1. The asset loader validates and compiles World-independent binding data. It
   preserves verb names and typed unresolved reference descriptors. Explicit
   reference kinds allow dependencies to be reported without consulting a live
   World from a staging worker.
2. The owner-thread instantiation pass resolves against one catalog and asset
   environment, producing `CompiledVerbBinding` with IDs, revisions, constants,
   and indexed input mappings.
3. Invocation fills dynamic slots and dispatches without JSON, name resolution,
   schema traversal, or asset loading.

The subtype's compiled value in `DataAssetCache` must contain no World-local
verb/tag/entity IDs, runtime pointers, or callable bound to a particular host.
Dependency preloading and retain/release use existing cache/lease contracts.
Do not accidentally preload arbitrary optional or dynamically selected resources;
document which constant references are eager dependencies and which remain
validated unresolved references.

`.sdata` is already a runtime format in this repository. Reuse that path; do not
invent a separate binary compiler solely for this ticket. A future cooker may
emit a compact symbol table, but instantiation must still relocate names against
the destination World. No `.sui` or `.sanim` version change is required here.

**Dispatch and admission**

Use one dispatcher entry point over a compiled binding and typed input view.
The implementation entry contains an erased concrete-object pointer and typed
trampoline, following `EngineSchedule`'s existing mechanism. An implementation
object receives only its dependencies during composition. The generic invocation
contains no `Engine&`, arbitrary resource lookup, service pointers, or subsystems.

Registration returns an explicit binding token. The host or implementation owner
unbinds through that token before destroying the target. Token checks include
generation so an old token cannot remove a replacement binding. The dispatcher
must outlive token users, or the token mechanism must make expired dispatchers
safe; choose and test one ownership contract rather than relying on declaration
order accidentally. Shutdown first disables admission, then removes bindings.

A synchronous admission response includes status and, when accepted, the minted
`InvocationId`. Required distinctions are accepted, unresolved/stale binding,
unavailable implementation, invalid arguments, refused request, and queue full.
Detailed diagnostic text can be emitted on failure; successful hot paths should
not format strings. Acceptance means the implementation took responsibility for
the request. It does not mean the requested gameplay outcome occurred.

There is no generic return payload, future, promise, or completion callback.
Those would create query/async-operation contracts beyond the demonstrated need.
Deferred execution may emit trace outcomes without exposing a generic result API.

V1 invocation is owner-thread only. Reject recursive entry into authored dispatch
with a clear diagnostic and no partial recursive execution. Implementations may
call their ordinary native APIs, but may not synchronously recurse through another
verb. A later orchestrator can schedule subsequent invocations with parent IDs.
Registration, unbinding, and target destruction must not happen inside a live
dispatch call.

**Timing, ECS, and bounded deferred work**

Registered dispatch is the common entry; execution semantics belong to the
operation. A callback may immediately do bounded work that is safe at every
supported call boundary. Simulation mutation and destructive lifetime transitions
must admit work into an appropriate scheduled operation. Do not let a producer
select `Immediate` to bypass that contract.

The initial contracts are:

| Operation | Admission and execution |
| --- | --- |
| `runtime.resume` | Requests the shell's existing deferred resume; page closure and pause reconciliation remain after action dispatch. Works with zero fixed ticks. |
| `application.quit` | Requests the host's existing exit path with the correct source attribution; does not destroy the engine or close the process from inside dispatch. |
| Game simulation proof operation | Owns a bounded typed queue, consumed at a documented fixed-logic boundary; validates the target again before mutation. |

Bind resume to the shell operation, not directly to a timescale write. Preserve
`PauseState::Effective()` and the network-session input-only behavior. A host
without an application shell can expose the name for discovery while reporting
the implementation unavailable.

For each deferred operation, document the drain phase, ordering edge, admission
cutoff, payload owner, capacity, overflow behavior, shutdown policy, and behavior
when the target is gone or dormant. The first simulation proof uses a detached
batch at its fixed-logic drain: requests present when draining begins are
processed in admission order; requests produced while processing that batch wait
for the next drain. No recursive drain and no unbounded relay chain in one tick.

Any configurable capacity or budget follows the CVar configuration convention.
Overflow rejects admission visibly rather than dropping an accepted request.
Do not add one queue per namespace. A queue exists because an operation crosses
a clock, mutation, or authority boundary, and may serve related typed operations
when they share one actual invariant.

Structural work records `CommandBuffer` commands and flushes only after all
relevant query scopes have ended. Name the concrete system that owns and flushes
the buffer. `EngineSchedule` does not do this automatically. Lifecycle hooks do
not acquire permission to enqueue unsafe recursive mutation through verbs.

World identity, `EntityId` generation, logic participation, and authority are
checked at execution. Default one-shot requests fail when their target is no
longer valid or eligible; do not silently wait for a later streamed incarnation.
An operation needing persistent-reference retry must explicitly own that policy.
Whole-prefab destruction, zone-state persistence, and spawn-group lifetime are
reasons not to introduce a naive generic `entity.destroy` as the first example.

No worker directly dispatches or appends to a shared owner-thread queue. Async
staging publishes through the existing async drain. If parallel producers are
introduced later, use partition-local plain records and deterministic owner-thread
merge order. Do not add locks, raw threads, or another executor.

**Invocation context, authority, and tracing**

Mint a monotonic World-local invocation ID only when an operation accepts a
request. Include an optional parent ID and optional diagnostic source attribution
such as binding key and producer entity. IDs are strong types; zero is absent.
The dispatcher can reserve a candidate before admission and allow gaps on
refusal, so an implementation can put the eventual ID into a queued record.

Do not require universal `Source` and `Target` entity fields. Operation targets
are typed arguments; producer entity inputs are explicitly mapped. A caller must
not be able to supply conflicting targets through two channels. Tracing metadata
must not decide gameplay authority.

An optional bounded trace ring records admission, dispatch, deferred execution,
failure, and parent identity. Store compact IDs in the hot path and resolve
labels for inspection. Preserve enough attribution across asset reload to explain
an old record without borrowing destroyed strings. Tracing can be disabled without
disabling identity propagation or allocating history.

Do not add generic authority flags, peer IDs, wire schemas, or replication policy
to the v1 invocation. A future network-aware implementation obtains session policy
through its own narrow dependencies, validates at the authority boundary, and
uses existing networking mechanisms. Acceptance on a client cannot confer
authority. World-local verb/entity IDs never become wire identities accidentally.

**Runtime and editor lifecycle**

Runtime startup changes are explicit:

1. Preserve component registration and schema sealing before entity creation.
2. Construct `RuntimeWorld` as today.
3. Install the shared registry and engine verb declarations. Install only the
   existing vocabulary resources a game hook is entitled to use; audit current
   `RegisterMovement`/AbilityKit composition rather than invoking unrelated
   systems to populate a picker.
4. Call `Game::OnRegisterVocabulary(World&)` in a checked registration scope.
   Finish this before content can resolve game-authored names.
5. Mount/publish content using existing assets and data subtype registration.
6. Construct the runtime dispatcher and bind host operations when shell/exit
   owners exist. Game startup/system composition binds game implementations.
7. Instantiate consumer bindings. A declaration may resolve before an
   implementation exists, but dispatch remains unavailable until binding.
8. Enable producers only after their binding and scheduling prerequisites hold.

The hook remains registration-only and editor-safe. It must not call `GetEngine`,
create entities, register components after the schema is sealed, or bind live
application services. Update `Game.h` to assign runtime invocation to the host
and audit examples/templates/tests for duplicate manual calls. A game defines
name/schema/argument contract once and references that definition from both
declaration and implementation binding. Two lifetime operations are necessary;
two independent lists of names and schemas are not.

Use existing `OnStart`/`OnRegisterSystems` composition to bind runtime operations;
no additional `Game` virtual hook is planned. Keep engine vocabulary installation
shared between runtime and editors, with explicit dependencies and no static
self-registration.

Kyusu installs engine declarations before its existing module vocabulary replay.
Shoji adds project-module loading and a metadata World, using
`GameModuleLoader` and the same hook. Do not start the game's normal lifecycle in
Shoji to obtain a picker. Create required metadata resources explicitly. Extract
shared editor composition only if actual duplication warrants it; avoid a new
editor application framework.

At shutdown, stop producers and admission, cancel/drop queued requests according
to their operation contract, release compiled bindings and their leases, unbind
implementations, destroy implementation owners and affected Worlds, and then
unmap module code. Module-owned destructors and compiler closures must execute
while the module remains mapped. Existing data subtype unregister rules still
apply. Metadata copies reduce callable lifetime exposure but do not erase the
existing module lifetime constraints of components and other World resources.

Module hot replacement is not v1. Reloading a module requires affected Worlds,
bindings, and module-owned cached values to be torn down and recreated under the
existing module lifetime rules. Catalog slot preservation is not evidence that
live DLL replacement is safe.

**UI integration and stock menu migration**

Add a proposed `UiVerbBindings` controller under `engine/include/ui/` with a
matching implementation. It depends on public UI values and the shared authored
mechanism, never gameplay implementations. It consumes a span/batch of actions
already drained by the host. Exactly one host drains a screen; logging and
bindings receive the copied batch rather than racing to consume it.

Keep UI event mapping separate from shared verb binding records. A UI mapping
names a local action, binding key, and explicit mapping from presentation argument
positions to declared input slots. Compile action names to screen-local IDs when
the screen opens. Associate the controller with the exact screen handle and
declaration revision. Reopen, declaration reorder, or screen destruction invalidates
that association. A hot-reloaded package that preserves the declaration can
reuse it only under the existing screen-lifetime contract.

The local action's producer signature belongs to the mapping/controller data for
v1; `UiScreenDesc::Actions` does not presently provide one. Validate both the
declared mapping and actual payload kinds. Do not infer arbitrary RML expression
types or convert `UiValue::Id` to an entity without an explicit typed mapping.

Stock menu migration must preserve native customization:

- Keep `PauseCommandId`, labels, enabled state, ordering, and native callbacks
  usable through the existing public model.
- Add an authored binding association for an entry. Default Resume/Quit entries
  obtain their operation from binding data, not a C++ switch or hardcoded
  name-to-row relationship.
- A native `SetHandler` explicitly replaces an entry's authored behavior, and
  assigning an authored binding explicitly replaces its native behavior. There
  is one active behavior per entry; never invoke both.
- Represent the stock entry-to-binding association in host menu data beside the
  shared binding asset. Display order remains presentation state. No subsystem
  registry appears in that data.
- Drain against the presentation's row-to-command snapshot. Publish a changed
  ordering only after the old action batch is handled, or invalidate that screen
  generation. A queued click must not acquire a different meaning when rows are
  reordered.
- Keep the Options navigation callback local for this landing. Future authored
  page navigation is a controller relation, not an engine verb requirement.

Audit all uses of `PauseMenuModel::Add`, `SetHandler`, `MoveBefore`, and
`InstallDefaults` before changing its representation. These headers are installed
SDK surface. Do not remove native customization as an incidental migration.

Store runtime behavior in `.sdata`, not `.preview.json`. The preview can reference
the same behavior asset and show resolved/unresolved bindings, but sample values
remain preview data. Runtime loading uses asset paths and the existing cache.
`UiPackage` remains presentation-only.

Shoji's first integration is read-only discovery and binding diagnostics. Do not
add a graph editor or new persisted editor interaction model in this ticket.
When editing arrives, authored records belong to a document and changes need
undoable commands and proper begin/preview/commit/cancel handling; direct mutation
of compiled bindings is not an authoring operation.

**Second producer: scheduled relay**

Add a minimal relay mechanism under proposed `engine/include/logic/` and matching
sources. A data-only component associates an entity with a binding asset and
stable binding key; runtime handles and resolved binding storage live in an
appropriate World resource/cache. Component registration, storage traits,
serialization, and handle lifecycle follow existing component contracts.

A concrete relay system accepts a bounded request identifying the relay entity
and typed input values. At fixed logic it resolves the relay's compiled binding
and invokes the same dispatcher used by UI. This ingress is a relay activation
mechanism, not a second vocabulary or dispatch API. It must not accept strings
naming subsystems or arbitrary native callbacks.

Provide one actual example/game composition that activates a relay through a
normal native gameplay path, with persisted binding data and a visible or
headlessly inspectable game-defined state change. Also invoke that same game
verb from UI with the same schema. A test-only synthetic producer is not enough
to declare the shared authored API proven.

Limit this stage to one-shot relay activation, explicit typed inputs, and fixed
draining. No physics overlap listener, timers, counters, recursive relay graphs,
or generic event source registry. Zero, one, and many matching relay entities
must work. Respect `ctx.Partitions`, retain no chunk pointers, and use a cached
query if processing scans component storage.

Exercise structural safety with a focused test implementation whose deferred
operation records and flushes `CommandBuffer` after a query. Do not ship an
underspecified entity-destruction verb just to obtain that test.

**Reload, missing names, and compatibility**

| Change/failure | Required behavior |
| --- | --- |
| Missing verb | Preserve name and arguments in authoring; mark binding unresolved; refuse invocation with a located diagnostic |
| Declared but unbound operation | Show vocabulary normally, report execution unavailable separately |
| Removed verb | Retire ID, invalidate executable binding, preserve source record |
| Changed schema | Increment contract revision and recompile affected bindings before reuse |
| Changed implementation | Replace only outside dispatch; invalidate binding generation; preserve semantic name if the contract is unchanged |
| Renamed verb | Treat old name as unresolved unless an explicit migration/alias exists; never guess by spelling |
| Unknown argument/field | Preserve in authoring round trips but reject executable binding if not accepted by the contract |
| Invalid asset reload | Retain the last valid published asset/binding where existing reload semantics permit it, and visibly identify the rejected revision |
| Valid reload removing a binding | Disable that binding; do not silently keep the old behavior executing |
| New catalog/World | Resolve again from names; never reuse the old compiled IDs |

Use `DataAssetCache::GetReloadVersion` and catalog revisions to invalidate derived
bindings. Rebuild at an owner-thread boundary, validate the replacement, and swap
atomically. Do not traverse source documents every frame. A cached revision
comparison is acceptable; recooking and schema traversal require an actual change.

Already accepted deferred requests carry their owned payload and recorded
contract. Ordinary content reload does not reinterpret them under new constants.
Implementation unbind or World/module teardown cancels or drains them before the
implementation disappears; the operation declares which. There is no generic
replay against a replacement implementation.

The authoring document retains raw unresolved records, while the executable cache
contains only validated bindings. This is deliberate source-versus-derived state,
not two authoritative copies. Shipping validation fails required unresolved
bindings; runtime still refuses safely if invalid content arrives.

The new subtype starts at version 1. Deterministic serialization and unknown-value
preservation need fixtures. No existing `.sui`, `.sanim`, scene, or data subtype
format is silently repurposed. Adding the relay component follows existing scene
schema registration; any necessary format change must be identified separately.

Aliases and migration tooling wait for a concrete rename. When introduced, aliases
must resolve unambiguously, diagnose deprecation, reject cycles/conflicts, and
never substitute incompatible schemas. Do not include a broad alias framework in
the first landing.

**Value endpoints and other later consumers**

Keep verbs, events, queries, and persistent values distinct. They may share schema
and identity conventions without sharing execution machinery.

The first value-endpoint work should adapt existing CVar metadata and use
`ConsoleRegistry::SetCVar`, preserving validation, authority, flags, latched
values, and archive behavior. Legal range and curated slider range are different
facts: Look Sensitivity currently exposes a narrower UI interval. Display-mode
choices should move into authoritative enumerable metadata rather than being
duplicated from a validator closure. Labels and curated choices may remain
presentation data. Do not force future gameplay values into CVars.

Animation markers will own marker timing and produce the same bindings; adding
markers first requires `.sanim` representation and loop/reverse/seek semantics.
Flowcharts will orchestrate events/queries and compile invoke nodes to the common
binding representation. They must not register an independent copy of each verb.
Queries need their own consistency/read-scope design when a real consumer arrives.
No global string event bus is introduced for any of these consumers.

AbilityKit is redesigned after UI and relay prove the substrate. Its current
activation queue, registries, serialization, and system boundaries are not
compatibility surfaces for this ticket. Future ability activation and authored
effects may use verbs at semantic boundaries while tight native internal calls
remain direct.

**Implementation stages and exit criteria**

Each stage is a reviewable change with its own tests. Stages are ordered; do not
start visual Shoji behavior authoring before the shared consumer gates pass.

| Stage | Material files and work | Exit criteria |
| --- | --- | --- |
| 1. Catalog and identity | Proposed `authored/VerbId` and `VerbRegistry`; shared schema validation additions; tests under `test/core/` | Atomic registration, deterministic enumeration, duplicate conflict, retirement/revival, revision and catalog-lifetime tests pass without Engine startup |
| 2. Typed binding and assets | Proposed `VerbArguments`, `VerbBinding`, `VerbBindingCompiler`; new data subtype composition; `assets/data/` integration | All advertised shapes compile or reject explicitly; dependencies and reference diagnostics work; shared asset loaded into two Worlds resolves independently; reload/version fixtures pass |
| 3. Dispatch and lifetime | Proposed `VerbDispatcher`, invocation values, optional trace sink; explicit runtime ownership | One checked entry point, narrow concrete targets, admission errors, no recursive dispatch, safe unbinding, owned queued payloads, causality and disabled-trace tests pass |
| 4. Vocabulary lifecycle | `app/Engine.cpp`, `app/Game.h`, runtime teardown; Kyusu document/module composition; module test fixture | Hook runs exactly once at the correct point, before content resolution; failure blocks startup; editor metadata installation starts no gameplay; teardown runs before module unload |
| 5. UI and shell proof | Proposed `ui/UiVerbBindings`; `app/PauseMenu*`; stock assets under `engine/assets/ui/`; host operation implementations in `app/` | Real headless UI event reaches authored binding and registered operation; zero-tick resume, exit request, row reorder, native overrides, and network pause semantics remain correct |
| 6. Relay and game proof | Proposed `logic/` component/system/resource; one example or template composition and its content; framework/runtime tests | Same game verb works from UI and persisted relay binding; fixed ordering, typed entity input, dormant/stale targets, bounded queue, explicit structural flush are verified |
| 7. Shoji discovery and diagnostics | `editor/shoji/src/ShojiServices.*`, authoring session/model integration; reusable editor module setup only where earned | Loaded game vocabulary appears without game startup; unresolved records survive; execution stays unavailable in metadata preview; module/document teardown tests pass |
| 8. Integration and documentation | `engine/CMakeLists.txt`, test CMake registration, ABI/isolation guards, affected UI/input/module docs | Full presets pass; public header fingerprint covers new surface; dependency fitness and disabled-feature build coverage pass; measured dispatch scenario recorded |

The implementation file names are proposals. Before adding a subtype registration
file, locate the actual engine data composition in the then-current tree. Use
`app/GameDataAssets.cpp` only for its existing game hook role; do not place all
engine subtype implementations there for convenience.

**Verification matrix**

| Contract | Required coverage |
| --- | --- |
| Identity | Empty/invalid names, conflicting duplicate, atomic failure, stable IDs after reorder, retired IDs, revived names, changed schemas, same numeric ID in two Worlds |
| Schemas | Required/default/null combinations, nested records/arrays, bad enum, numeric overflow/nonfinite values, explicit UI conversion failures, entity string identity, asset kind/subtype/tag resolution |
| Asset lifecycle | Existing stage/commit path, dependencies, retain/release, load into separate Worlds, reload replacement, unknown-name round trip, version refusal, no World access on staging workers |
| Dispatch | No implementation, stale revision, stale binding token, reentrant refusal, target lifetime, no borrowed queued payload, bounded refusal, trace enabled/disabled |
| UI | Actual RML action through headless UiService; screen-local ID collision; wrong payload; close/reopen; declaration reorder; row reorder with queued action; one drain consumer |
| Shell | Resume with zero ticks, deferred page closure, Back behavior, native callback override, disabled/removed entries, exit attribution, live-session input-only pause |
| Scheduling/ECS | Admission before/during/after drain, catch-up ticks, zero ticks, zero/many relays, explicit ordering, active-query invocation, safe structural flush, dead entity generation, dormant partition, teardown with pending work |
| Modules/editors | Runtime hook ordering/failure, metadata-only World, no GetEngine dependency, multiple documents, documents created before module install, module-owned callable destruction before unload |
| Performance | Repeated dispatch uses IDs/indexed mappings, no JSON/schema/name work; bounded queue work; no successful no-argument invocation allocation after setup; typed dynamic payload costs recorded |

Use existing `UiModelTests`, `UiHotReloadTests`, `OptionsPageTests`,
`ModuleVocabularyTests`, `GameModuleLoaderTests`, `DataAssetTests`,
`InputActionResolveTests`, and `EngineScheduleTests` as relevant precedents and
regression suites. Add focused new files by owning mechanism, not one large
authored-API test file. Discover actual CTest names before choosing filters.

Measure warmed no-argument dispatch, scalar/entity argument dispatch, queued
admission/drain, and the end-to-end UI path. Report build configuration, counts,
allocation behavior, and timings. Expected lookup work is O(1) per invocation
plus O(dynamic argument data) validation/copying; queue drain is proportional to
the admitted bounded batch. No speedup claim is required or justified without a
baseline. V1 producers run on the owner thread; exercise worker-count zero and a
normal worker configuration for integration equivalence. If producer parallelism
is added, deterministic merge tests and relevant TSan coverage become mandatory.

Run the repository's canonical implementation workflow:

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
git diff --check
```

Run focused tests during each stage, then the full serial CTest suite at the
integration gate. Add module ABI/layout/isolation coverage and relevant editor/UI
dependency fitness. Include the new public `authored/` surface in
`SENCHA_ABI_FINGERPRINT_HEADERS` in `engine/CMakeLists.txt`; changing `Game.h` and
schema enums also affects the public fingerprint. Reusing the hook avoids a new
vtable slot, not SDK compatibility obligations. Do not promise arbitrary
compiler/standard-library ABI interoperability for the registered C++ operations.

Verify the core catalog/compiler/dispatcher build without UI enabled. Run UI
tests headlessly using existing support. Do not claim a complete Vulkan-disabled
build unless that configuration actually succeeds; unrelated existing failures
must be recorded rather than worked around through a new dependency path.

**Risks and implementation decision gates**

The architecture decisions above are settled for this plan. The following narrow
choices must be resolved with code/tests at the named stage, not left implicit:

- Stage 1: exact provider registration-scope API and schema-equivalence algorithm.
  It must enforce atomic failure through the existing void game hook.
- Stage 2: concrete argument storage layout and entity metadata extension. They
  must share `DataFieldSchema`, avoid arbitrary JSON integer identity, and support
  the two proving consumers without a generic expression language.
- Stage 3: explicit binding-token lifetime mechanics. Unbinding cannot dereference
  a destroyed dispatcher or leave a live pointer to a destroyed implementation.
- Stage 4: initialization of existing non-verb vocabulary resources before the
  game hook. Do not accidentally seal/register component types in the wrong order
  or preserve prototype AbilityKit architecture as a requirement.
- Stage 5: persisted stock menu entry association and API-compatible native
  override representation. Existing customization and queued-row meaning need
  regression evidence before migration.
- Stage 6: concrete example game and proof verb, selected from current examples.
  It must demonstrate real state ownership without expanding into a full game
  mechanic, trigger system, or AbilityKit rewrite.

Public ABI changes, new persisted subtype contracts, and shell behavior changes
must be called out in their implementation reviews. If a stage requires a larger
format migration, major subsystem replacement, or different product semantics,
stop and revise that stage rather than hiding the change behind an adapter.

**Definition of done**

The work is complete when one authored vocabulary is discoverable in runtime and
metadata Worlds; names compile to safe World-local bindings; UI and a real
scheduled relay invoke the same typed game operation; the shell proof preserves
pause/exit behavior; and timing, reference lifetime, module teardown, authority,
and ECS mutation retain identifiable owners. Missing content remains inspectable
and cannot silently execute a different operation.

No consumer selects a subsystem registry. No central dispatcher accumulates
subsystem dependencies or behavior branches. No new API is required merely to
add another producer. All applicable verification above passes, or the handoff
states exactly what remains unverified and why. Later visual behavior authoring,
values, animation markers, queries, events, and AbilityKit redesign remain
separate deliveries over this foundation.
