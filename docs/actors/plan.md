# Sencha Hybrid Actor System Plan

Status: **proposed working plan** (2026-06-23).

This document plans a thin *actor facade* over the archetype ECS, for the
narrow class of entities the ECS handles worst: low-count, high-bespoke,
stateful, sequenced, cross-referential authored objects — doors, levers,
elevators, encounter scripts, bosses, set-pieces. It is **not** a replacement
for systems, and it is **not** for the uniform bulk (props, pickups,
projectiles, particles), which stay pure data-oriented ECS.

The north star:

> Bespoke authored objects get the locality and direct references of an
> actor+component model, while their data still lives in chunks and still flows
> through the existing transform, render, serialization, and streaming systems.

## Why (the boundary, not a religion)

The ECS wins on **many + uniform**; it loses on **few + bespoke**. Five
representative authored-logic problems, scored honestly:

| Problem | Winner | Why |
|---------|--------|-----|
| Lever → door (one relationship) | Tie | ECS `Activatable` pattern if reactions are uniform; actor if heterogeneous |
| Room-lockdown encounter sequence | **Actor** | one-shot, sequenced, irregular state (no `std::vector` in a component) |
| Boss with N phases | **Actor** | count = 1, deeply bespoke; ECS pays all indirection for zero batch payoff |
| Persistent ability pickup | **ECS** | many, uniform, data-driven; state via the zone overlay |
| 500 projectiles | **ECS** | high-count uniform; the actor path here is the failure mode |

The dividing axis is **count × bespoke-ness**. The litmus test: *the moment you
reach for a `switch (kind)` in a system, or fake per-instance state with
fixed-size component fields or a side table — that entity wants the actor
facade.* The facade exists to serve exactly the two "Actor" rows above without
disturbing the two "ECS" rows.

## Current Foundation

What already exists and is reused (no changes required to land the substrate):

- `World` (`engine/include/ecs/World.h`): archetype storage; `CreateEntity`,
  `CreateEntityWithSignature`, `AddComponent`, `DestroyEntity`, `TryGet<T>`,
  `IsAlive`; per-`World` resources via `AddResource<T>` / `TryGetResource<T>` /
  `GetResource<T>` / `HasResource<T>`.
- `Registry` (`engine/include/world/registry/Registry.h`): `World Components`,
  `ResourceRegistry Resources`, `RegistryEntityFacade Entities`. A zone is a
  `Registry`; it is movable for async handoff.
- `EngineSchedule` (`engine/include/app/EngineSchedule.h`): `Register<T>(...)`,
  concept-detected phase methods, `After<T, TDep>()` ordering.
- Frame contexts (`engine/include/app/GameContexts.h`): each phase receives a
  context carrying `FrameRegistryView Registries` and
  `std::span<Registry*> ActiveRegistries`. `FixedLogicContext` is the
  deterministic gameplay phase.
- `Game::OnRegisterSystems(SystemRegisterContext&)`
  (`engine/include/app/Game.h`): the game's system-registration hook.
- `CommandBuffer` (`engine/include/ecs/CommandBuffer.h`): deferred structural
  mutation for in-query writes.
- `ComponentTraits<T>` lifecycle hooks (`OnAdd`/`OnRemove`), used today by
  `StaticMeshComponent` and `AudioSourceComponent` for retain/release.
- Scene serialization (`engine/src/world/serialization/SceneSerializer.cpp`,
  `world/ComponentManifest.h`): schema-driven, folds over
  `EngineSceneComponents`; `hierarchy` already serializes by entity array index.

## Non-Goals

- No behavior for the uniform bulk. Props, pickups, projectiles, particles do
  **not** get a `ScriptComponent`. Giving everything one rebuilds a slow actor
  engine on a fast ECS.
- No virtual `Tick` dispatched over an ECS query. Actor logic is driven from a
  side table, never from chunk iteration.
- No new scripting language in the substrate. Authoring front-ends (behavior
  trees, coroutine scripts, flowcharts, I/O wiring) are deferred and lower onto
  this substrate later (see D-G).
- No cross-zone actor references in v1. Actors reference entities within their
  own registry; cross-zone links are the partition layer's concern.
- Not a replacement for systems. Systems remain the home for everything
  many-and-uniform; actors complement them.

## Vocabulary

`Actor`
: A non-owning value handle `{ World*, EntityId }` with ergonomic component
  access. Zero storage, no vtable.

`ActorLogic`
: A plain heap behavior type for one bespoke entity (no base class), validated
  by the `ActorLogic` concept. Holds rich per-instance state freely (it is not a
  component). Defines any of `BeginPlay`/`Tick`/`OnInput`/`EndPlay` it needs;
  each callback is opt-in (see D-I).

`ActorKind`
: The per-kind dispatch record stamped at `RegisterKind<T>()`: the factory
  (`Make`/`Destroy`) plus function pointers for the callbacks `T` defines, null
  for those it omits. The runtime-heterogeneous equivalent of a vtable, built
  the way `EngineSchedule` erases systems (see D-I).

`ScriptComponent`
: A trivially-copyable component linking an entity to its logic: `KindId` (the
  serialized factory key) and `LogicSlot` (runtime-only index into the table).

`ActorLogicTable`
: A per-`World` resource owning the live logic instances (type-erased state +
  the owning `ActorKind`), their owner `EntityId`s, a free list of slots, and
  the pending BeginPlay/EndPlay queues.

`Kind` / `KindId`
: A named logic type registered via `RegisterKind<T>()` (`DoorLogic`,
  `BossLogic`), which produces its `ActorKind` record. `KindId` is what
  serializes; `ActorKind::Make` rebuilds the object on load.

`Connection`
: An authored output→input link: `{ EntityId Target, uint16 OutputId,
  uint16 InputId, float Delay }`. Serialized by entity array index.

`ActorScriptSystem`
: The single system (FixedLogic phase) that drains lifecycle queues, ticks live
  logic, and delivers due I/O events — iterating the per-registry tables over
  the Logic span, not an ECS query.

## Decisions

### D-A — Behavior lives in heap logic objects beside the chunks

Components are `static_assert`-ed trivially copyable, so behavior cannot live in
them. A bespoke entity carries a POD `ScriptComponent { KindId, LogicSlot }`;
the actual logic object — a plain `ActorLogic`-conforming type with arbitrary
state (`std::vector`, sub-state-machines, timers) — lives in the
`ActorLogicTable` resource. This keeps the component relocatable for chunk
storage while giving behavior a real home with locality. How those types are
dispatched without a base class is D-I.

### D-B — Actors are driven from a side table, not an ECS query

`ActorScriptSystem` iterates `ActorLogicTable` slots directly, not a
`Query<...>`. Consequence: actor logic runs **outside any active query scope**,
so it may use direct `World` structural mutation (spawn, destroy, add/remove)
through the `Actor` handle — no `CommandBuffer` ceremony for an actor mutating
itself or spawning children. This matches the existing precedent (D3.5: direct
structural mutation outside an active query is legal; `FreeCamera` and
`CubeSpinSystem` mutate `LocalTransform` via `TryGet` in fixed/update phases).
If an actor's logic itself opens a query and mutates mid-iteration, the normal
ECS rule applies and it must use a `CommandBuffer`.

### D-C — Opt-in per entity; the bulk stays pure ECS

Only entities with a `ScriptComponent` are actors. The table holds the bespoke
minority; the high-count uniform entities never enter it and pay nothing. This
is the boundary from the scorecard, made structural: there is no global "tick
all entities" path.

### D-D — Lifecycle is deferred to phase boundaries via queues

`BeginPlay` and `EndPlay` may need structural mutation (spawn children, wire
references), which lifecycle hooks forbid (D1.4). So:

- New `ScriptComponent`s are enqueued for `BeginPlay`, run once at the next
  `ActorScriptSystem` tick (outside query scope, structural allowed).
- `ComponentTraits<ScriptComponent>::OnRemove` only **records** the slot into a
  pending-EndPlay list (no structural mutation in the hook); the system drains
  it, calls `EndPlay`, then frees the slot and releases the logic object.

### D-E — I/O is a scene-level connection table with delayed, fixed-tick delivery

Output→input wiring is authored as `Connection` records and lives in a
per-registry table (resource), not as pointers. `Scene::Fire(source, outputId,
params)` fans out to wired targets and enqueues `{ target, inputId, params,
fireAt }` in an event queue; `ActorScriptSystem` delivers due events on fixed
ticks to `logic->OnInput`. Delivery is deterministic and frame-accurate, and is
the natural first consumer of the action-adventure plan's "frame-accurate
authored event queue" timing surface. A scene-level table (vs a per-source
component) supports editor authoring and fan-out cleanly.

### D-F — Serialization reuses the existing scene model

`ScriptComponent` joins `EngineSceneComponents` in `world/ComponentManifest.h`
with a `TypeSchema` declaring `Name`, `SceneChunkId`, and the `KindId` field.
`LogicSlot` is runtime-only and omitted from the schema (same rule as
`AudioSourceComponent::Voice`). A post-load pass instantiates each logic object
via the `KindId` factory and assigns its slot. `Connection`s serialize as a new
top-level scene section keyed by entity array index, exactly like `hierarchy`.

### D-G — The authoring front-end is separate and deferred

The substrate (the `ActorLogic` contract + the event bus) is the **runtime
contract**.
Behavior trees, coroutine scripts, visual flowcharts, and Hammer-style I/O
wiring are all front-ends that lower onto it. Build the C++ substrate first; it
already solves the boss and encounter problems in code and is what every
front-end needs underneath. Pick a front-end only once content shape is known
(see Open Questions) — it is the expensive, hard-to-reverse part.

### D-H — The `Actor` handle splits const/non-const access

`Actor::Get<T>()` (non-const) returns `T*` and registers a change (mutable ECS
access bumps the column version, D4.4); `Actor::Get<T>() const` routes through
`std::as_const(World)` and does not bump. Logic that reads without intending to
dirty must use the const path, same discipline as the rest of the engine.

### D-I — Dispatch is a concept plus a per-kind function-pointer record

Logic types are plain structs validated by an `ActorLogic` concept; per-kind
dispatch is a stamped function-pointer record (`ActorKind`) built at the typed
`RegisterKind<T>()` site. No abstract base class, no vtable. Each callback is
detected by its own concept (`HasBeginPlay<T>`, `HasActorTick<T>`,
`HasOnInput<T>`, `HasEndPlay<T>`) and left null in the record when `T` omits it,
exactly as a system implements only the schedule phases it needs.

```cpp
template <typename T>
concept HasActorTick = requires(T& t, Actor a, ActorContext& c) { t.Tick(a, c); };
// ...HasBeginPlay / HasOnInput / HasEndPlay

struct ActorKind {                                   // one per registered kind
    KindId Id;
    void* (*Make)();   void (*Destroy)(void*);
    void  (*BeginPlay)(void*, Actor, ActorContext&)                    = nullptr;
    void  (*Tick)     (void*, Actor, ActorContext&)                    = nullptr;
    void  (*OnInput)  (void*, Actor, const InputEvent&, ActorContext&) = nullptr;
    void  (*EndPlay)  (void*, Actor, ActorContext&)                    = nullptr;
};

template <typename T> ActorKind MakeActorKind(KindId id) {
    ActorKind k{ id, [] { return static_cast<void*>(new T{}); },
                     [](void* p) { delete static_cast<T*>(p); } };
    if constexpr (HasActorTick<T>)
        k.Tick = [](void* p, Actor a, ActorContext& c) { static_cast<T*>(p)->Tick(a, c); };
    if constexpr (HasOnInput<T>)
        k.OnInput = [](void* p, Actor a, const InputEvent& e, ActorContext& c)
                    { static_cast<T*>(p)->OnInput(a, e, c); };
    // ...BeginPlay / EndPlay
    return k;
}
```

`ActorLogicTable` stores per-instance `void* State` plus the owning
`ActorKind*`; the system skips null callbacks. `Make`/`Destroy` make the record
double as the `KindId` factory used by spawning (D-D) and load (D-F), so
data-driven creation and dispatch share one registration.

**Rationale.** The engine already resolves "typed registration site +
runtime-heterogeneous storage" this way three times: `EngineSchedule` phase
dispatch, `CommandBuffer` lifecycle-hook stamping, and `ComponentTraits` concept
detection (D2.2). A virtual `IActorLogic` would be the only inheritance
hierarchy in the systems/components layer.

**Explicitly not a performance decision.** `record->Tick(state, …)` is two
indirections, equivalent to a vtable. A3 ("no virtual calls in hot paths") does
not apply — actor tick is the cold path. The choice buys idiomatic consistency
and a flat, inheritance-free type model, not speed; a virtual interface would
have been a defensible simpler alternative on readability grounds alone.

## Rollout

Ordered so each stage is independently testable and lands value. Stages 1–4 are
the front-end-agnostic substrate; Stage 6 is the deferred fork.

### Stage 1 — Handle + minimal substrate

`Actor`, the `ActorLogic` concept + `ActorKind` dispatch record (D-I),
`ScriptComponent`, `ActorLogicTable`, and `ActorScriptSystem` (Tick only).
`RegisterKind<T>()` plus the `KindId` registry.

Gate: a test registers a kind, creates an entity with a `ScriptComponent`, and
sees `Tick` called with an `Actor` that can read and write a sibling component.

### Stage 2 — Lifecycle

Pending BeginPlay/EndPlay queues; `ComponentTraits<ScriptComponent>::OnRemove`
records the slot; factory-driven construction.

Gate: spawn → `BeginPlay` runs once with structural access (can add components /
spawn children); destroy → `EndPlay` runs once before the slot is freed; no
leaks under the asan/refcount test.

### Stage 3 — Event/I/O bus

`Connection` table, name hashing, `Scene::Fire`, delayed event queue, delivery
to `OnInput` after Tick.

Gate: a lever fires an output wired to a door's input; the door's state changes
after the declared delay; delivery is deterministic across fixed ticks.

### Stage 4 — Serialization

`ScriptComponent` into `EngineSceneComponents` with `TypeSchema`; connections as
a scene section by entity index; post-load instantiation and reference
resolution.

Gate: save a scene (lever + door + connection), reload, and the I/O still fires
and behaves identically — round-trip test.

### Stage 5 — Spawn ergonomics & actor queries (companion)

Land `CommandBuffer::Spawn<Ts...>` (the create-with-components path) so
non-actor systems can spawn actors mid-query, plus `Scene::FindTagged` for
BeginPlay wiring.

Gate: the room-lockdown encounter (scorecard #2) implemented end-to-end as an
example/test.

### Stage 6 — Authoring front-end (deferred; decide first)

Choose behavior trees / coroutine scripts / flowchart / wiring based on the
Open Questions; the chosen front-end lowers to the `ActorLogic` contract + the
event bus.

Gate: one authored example reproduces the boss (#3) or encounter (#2) without
hand-written C++ logic.

## Open Questions

- **Keeping ECS the default.** Actors are an opt-in exception by policy, but
  affordances beat policy: if writing an actor is easier than defining POD
  components plus a system, developers will default to actors and erode the
  data-oriented core — the Unreal "everything is an `AActor`" failure mode. This
  matters because dispatch parity (D-I) is per-call only; systems still batch
  SoA over chunks and crush per-actor dispatch at scale, so over-adoption is a
  silent performance regression, not just a style drift. Open: how to balance
  ergonomics so the ECS path is not the heavier one (give systems/components
  sugar comparable to the `Actor` handle); whether examples and the first
  tutorial lead with pure ECS and frame actors as the exception; and whether to
  make over-adoption a *measurable* content risk — e.g. flag a `KindId`
  instantiated above a threshold, or a zone where actors outnumber plain
  entities — surfaced through the action-adventure content-risk dashboard.
- **Authoring front-end.** Who authors logic — programmers or non-programmer
  designers? And what is the dominant shape — reactive AI (→ behavior trees),
  scripted sequences/set-pieces (→ coroutine scripts or a timeline), or heavy
  object wiring (→ I/O bus)? These pick Stage 6. Current lean: behavior
  trees for AI/bosses + coroutine sequences for set-pieces + lightweight wiring
  for connections; hold off on a general visual flowchart until content demands
  free-form graphs.
- **Table home.** `ActorLogicTable` as a `World` resource (matches
  `StaticMeshComponentAssets`) vs `Registry.Resources`. Lean: `World` resource.
- **System registration.** Engine built-in (always present, zero-cost when a
  registry has no table) vs game-registered via `OnRegisterSystems`. Lean:
  engine built-in alongside `DefaultRenderPipeline`/`AudioSystem`.
- **Connection representation.** Scene-level table (this plan) vs per-source
  component. Lean: scene-level for editor authoring and fan-out.
- **Cross-zone identity.** Stable actor identity for state overlays and
  cross-zone links ties into the action-adventure plan's stateful streaming;
  out of scope for v1 but should not be designed against.
- **Event ordering.** Determinism of delivery across multiple active registries
  in one tick.

## Architectural Smells

Be suspicious when new code:

- gives a high-count or uniform entity a `ScriptComponent`
- adds a "tick every entity" path or dispatches actor logic from an ECS query
- stores rich/irregular state in `ScriptComponent` instead of the logic object
- performs structural mutation inside `ComponentTraits<ScriptComponent>` hooks
- caches chunk row pointers in a logic object across frames without keying
  invalidation off `World::StructuralVersion()`
- wires actors with raw `EntityId`s across zones that break on unload
- does heavy per-event work in `OnInput` delivery without a budget
- reaches for a visual flowchart before the substrate exists
