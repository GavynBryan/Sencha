# AbilityKit — Sencha's Gameplay Framework

Status: **proposed working plan** (2026-06-24). Version assignments and gates live in `docs/plans/engine-roadmap.md` (Track A).

This document plans **AbilityKit**, Sencha's gameplay framework, built the
data-driven ECS way: bespoke gameplay behavior lives in **authored data**
(tags, attribute tables, effect and ability definitions) interpreted by
**uniform systems** over **POD components**. It deliberately rebuilds the
*concepts* of Unreal's GAS — attributes, gameplay effects, abilities, gameplay
tags, gameplay cues — without its object hierarchy (`AbilitySystemComponent`,
ability/effect `UObject`s). There is no per-actor "ability object"; an entity's
gameplay capability is its component composition plus the data assets it
references.

The north star:

> Combat, status effects, resources, and cooldowns are authored as data and
> resolved by a handful of ordered systems. Mutual exclusion comes from tags,
> not booleans and not a state machine. Nothing pushes; systems pull.

## Why data-driven, not an actor/object model

This plan is the conclusion of a longer design pass (the hybrid-actor plan was
scrapped). The relevant findings:

- **Bespoke behavior belongs in data, not compiled per-entity objects.** A boss,
  an ability, a status effect are *data* interpreted by one system, which keeps a
  single authorship model, stays designer-editable and hot-reloadable, and avoids
  the affordance-drift that an actor facade invites.
- **Tags are the mutual-exclusion mechanism, not an HSM.** Abilities are *not*
  mutually exclusive as a class — a buff, a movement ability, and an attack can
  all be active at once. Conflicts are pairwise and declarative
  (`BlockedByTags` / `CancelAbilitiesWithTag`), which a single-active-state HSM
  cannot express. An HSM is only ever warranted for a separate, optional
  locomotion/stance concern; it is out of scope here.
- **GAS is already half data-driven in spirit** (effects/abilities/tags are
  data-ish even in Unreal). ECS just makes the runtime data-oriented too.

## Current Foundation

What exists and is reused:

- **Gameplay tags module** (landed on this branch at
  `engine/include/core/gameplay_tags/` — to be relocated to the `framework/`
  area per D-J): `GameplayTagId` (a `StrongId`), `GameplayTagRegistry`
  (dot-path hierarchy, auto-parent creation, `IsDescendantOf`, interning),
  `GameplayTagSet` / `CountedGameplayTagSet` (heap-backed; usable as world state,
  **not** as components), and `GameplayTagQuery` (All/Any/None, Exact/Hierarchical
  match). Has unit tests; not yet wired to the ECS.
- **ECS** (`engine/include/ecs/World.h`): POD components
  (`static_assert(is_trivially_copyable)`), `RegisterComponent`,
  `AddResource`/`GetResource`/`TryGetResource<T>` (registry-local resources),
  `Changed<T>` change detection, `CommandBuffer` for deferred structural change.
- **Scheduling** (`EngineSchedule`, `GameContexts.h`): systems are plain types
  with concept-detected phase methods (`FixedLogic(FixedLogicContext&)` …),
  registered via `Register<T>`, ordered with `After<T, TDep>`; contexts carry
  `std::span<Registry*> ActiveRegistries`.
- **Serialization** (`world/ComponentManifest.h`, `SceneSerializer`):
  schema-driven over `EngineSceneComponents`; runtime-only fields omitted
  (`AudioSourceComponent::Voice` precedent); handles serialize as asset paths.
- **Assets** (`AssetSystem`, caches): data assets referenced by handle — the home
  for effect and ability *definitions*.

## Non-Goals

- **No `AbilitySystemComponent` god-object.** Capability is composition +
  systems, never one big per-entity object.
- **No HSM for ability exclusion** (see rationale above).
- **No observer/callbacks.** Reactions are systems that poll data (conditions,
  facts, queues, event entities).
- **No networking / replication / prediction.** GAS's replication model is out of
  scope; single-player authored play first, matching the action-adventure plan.
- **No real physics, animation, or VFX dependency.** Knockback, hit windows, and
  cues integrate through thin sinks AbilityKit owns (D-J); they are stubbed until
  those systems exist, so AbilityKit is buildable in parallel.
- **No authoring front-end.** Definitions are authored as data files (JSON/asset)
  first; a visual editor is a later, separate concern.

## Vocabulary

`Attribute`
: A named float resource (Health, Stamina, Poise, AttackPower). Identified by a
  compact `AttributeId`; defined in a data table.

`AttributeSet`
: A POD component holding an entity's attributes as a flat block of
  `(AttributeId, Base, Current)`.

`Effect`
: A data-defined modification — instant (damage/heal to `Base`), duration, or
  infinite (active modifiers folded into `Current`), optionally periodic, with
  granted tags and a stacking policy. The GAS `GameplayEffect` analog.

`ActiveEffect`
: A runtime **entity** representing one applied duration/infinite effect on a
  target, batch-processed and despawned on expiry.

`Ability`
: A data-defined activatable action: activation tags, require/block tag query,
  cost effect, cooldown effect, granted-while-active tags, and a behavior.

`AbilityGrants`
: A POD component listing what an entity may run: one grant per ability binding
  (see "Grants and input").

`GameplayTagContainer`
: A POD per-entity tag component (fixed-cap sorted ids + stack counts) — the
  ECS-storable replacement for the heap-backed `GameplayTagSet`.

`Intent`
: A decoupled request to activate an ability, produced from input/AI, consumed by
  the activation system.

## Decisions

### D-A — Tag registry is a resource; per-entity tags are a POD component

`GameplayTagRegistry` lives as a per-`World` (or global) resource — one instance,
heap-backed storage is fine. Per-entity tags become a new POD component:

```cpp
struct GameplayTagContainer {              // trivially copyable → lives in chunks
    static constexpr int Cap = 32;
    GameplayTagId Tags[Cap];               // sorted
    uint8_t       Counts[Cap];             // stack count; revoke decrements, 0 removes
    uint8_t       Count = 0;
};
```

Hierarchical membership tests take the registry resource (`IsDescendantOf`). The
heavy provenance in `CountedGameplayTagSet` (`GameplayTagSource` strings, grant
handles) is **not** carried per-entity: the `ActiveEffect` entity that granted a
tag *is* its provenance, and it owns the grant/revoke. The existing
`GameplayTagSet`/`CountedGameplayTagSet` remain available for non-ECS / world
state, but are not used as components.

### D-B — Tags carry mutual exclusion; no state machine

Mutually exclusive and composable conditions alike are tags. "Without booleans"
means `Has(State.Stunned)` instead of `bStunned`, and ability conflicts are
declarative tag relations on the ability definition, not a global active-state
enum. No HSM.

### D-C — Attributes are a POD block plus a data-driven definition table

```cpp
struct AttributeSet {                      // POD block component
    static constexpr int Cap = 16;
    AttributeId Ids[Cap];
    float Base[Cap];                       // persistent; instant effects modify this
    float Current[Cap];                    // Base ∘ active modifiers, recomputed each tick
    uint8_t Count = 0;
};
```

Attribute *definitions* (id, name, clamp range, default) live in an
`AttributeRegistry` resource, authored as data — so designers add attributes
without engine code. `Current` is derived; `Base` is authoritative.

### D-D — Effects are entities; instant hits Base, duration folds into Current

- **Instant** effects (damage, heal) apply once to `Base` and leave no entity.
- **Duration / infinite** effects spawn an `ActiveEffect` entity referencing the
  target by `EntityId`. What is current is in "Effects" below.
- `EffectLifetimeSystem` counts effects down and ends them; `AttributeResolveSystem`
  recomputes each target's `Current` from `Base` plus all active modifiers, then
  clamps to the attribute's range.

### D-E — Abilities are data + one activation system; cost/cooldown are effects

*Superseded by phased abilities ("Ability definitions", "Runtime").*

An `AbilityDef` data asset carries: activation tag, a `GameplayTagQuery`
(require/block), a cost effect ref, a cooldown effect ref + duration + cooldown
tag, granted-while-active tags, and a behavior. `AbilityActivationSystem`:

1. drains activation `Intent`s;
2. checks the require/block query against the entity's `GameplayTagContainer`
   (the cooldown tag is just a block tag);
3. checks cost affordability against the `AttributeSet`;
4. on success: applies the cost effect, applies the cooldown effect (which grants
   the cooldown tag for its duration), grants active tags, and runs the behavior.

Cancellation is a tag operation: an ability with `CancelAbilitiesWithTag` revokes
the active tags of conflicting abilities. No central arbiter, no HSM.

### D-F — Events are data a system pulls, never callbacks

Four forms by lifetime: derivable **conditions** (queried, e.g.
`Changed<Health>` + compare — stored nowhere); durable **facts** (a `Dead`
tag/component); transient **notifications** (a double-buffered `Events<T>` queue
resource); per-occurrence **payloads** (event entities, spawn→process→despawn).
"Health ≤ 0 → die" is a condition that produces a durable `Dead` tag plus an
optional `DeathEvent` in a queue. Never add/remove a transient flag component per
frame (archetype churn); clear event data in bulk instead.

### D-G — Behavior reaches the world through thin sinks AbilityKit owns

An ability's effects on attributes and tags are pure data. Its effects on the
*world* — impulse/knockback, montage + hit-window timing, overlap/trace hit
detection, cosmetic cues — go through narrow interfaces (`IImpulseSink`,
`IMontageSink`, `IHitQuery`, `ICueSink`) that AbilityKit defines and that physics /
animation / render / audio implement later. Stubs now; real implementations when
those systems land. AbilityKit must not reach into those backends directly.

### D-H — Serialize tags and attributes by name, definitions by asset path

`GameplayTagContainer` and `AttributeSet` serialize their entries by **registered
name**, resolved to ids through the registry on load (ids are registration-order
dependent, names are stable) — the same pattern as asset handles serializing as
paths. Effect/ability *definitions* serialize as asset-path handles. Active
effects and resolved `Current` values are runtime-only (omitted from schema);
persistence across save/streaming is the overlay's job, deferred.

### D-I — Input is decoupled from activation via intents

An input/AI mapping layer produces activation `Intent`s (a queue resource or a
small per-entity intent component); `AbilityActivationSystem` consumes them. The
ability system never reads raw input, so the same system serves player and AI.

### D-J — AbilityKit lives in a delineated `framework/` area, decoupled by rule, not a separate library

Gameplay is not core engine behavior, but the separation that matters is a
*dependency rule*, not a build target. All AbilityKit code — gameplay tags, attributes,
effects, abilities, quest/state, and their systems — lives under a delineated
`framework/` area compiled into the existing `sencha_engine` library
(`engine/include/framework/…`, `engine/src/framework/…`), kept apart from
`core/`, `render/`, `graphics/`, and scene serialization by directory and rule.

A separate `sencha_framework` target was considered and rejected for now: it
would link the whole monolithic engine, so it would **not** prevent framework
code from including `render/`/`scene` — the decoupling we actually want — while
only enforcing the reverse (engine ↛ framework), which the engine already handles
for every module via directory discipline and the dependency rules in
`core-systems-map.md`. A separate target adds build, export, and
test/example-linking cost (the bloat we are avoiding) without buying the property
we care about.

The decoupling is enforced three ways, all cheap:

1. **Dependency rule.** Framework code may include `core/`, `ecs/`, `math/`; it
   must **never** include `render/`, `graphics/`, or scene-serialization headers.
   Engine `core/`/`render` must never include `framework/`.
2. **Thin sinks (D-G).** The framework defines `IImpulseSink`/`IMontageSink`/
   `IHitQuery`/`ICueSink`; the game (not the framework) wires them to render,
   audio, physics, and animation — so the framework has no reason to include
   those backends.
3. **A guard test.** A small include-grep test (or CI check) fails if a
   `framework/` file pulls in `render/`/`graphics/`/scene headers, or if any
   engine module includes `framework/`. This enforces *both* directions directly
   — something a separate library would not.

Promote to a standalone `sencha_framework` library later only if the engine is
split into fine-grained modules (so the framework can link a gameplay-safe
subset), if you want to ship the engine without the framework, or if gameplay
build times warrant an incremental boundary. None of those hold today.

The tags currently under `engine/.../core/gameplay_tags/` move into the
`framework/` area as the first task of Stage 1; `StrongId` and real core
utilities stay in `core/`.

## Frame Ordering

All in `FixedLogic` (deterministic), ordered with `After<>`:

1. `ActorTimeSystem`, then `AbilitySystem` — see "Runtime".
2. `EffectApplicationSystem` — drain apply-effect requests: instant → `Base`,
   duration → spawn `ActiveEffect`.
3. `EffectLifetimeSystem` — tick durations, periodic ticks, expiry, tag
   grant/revoke.
4. `AttributeResolveSystem` — recompute `Current` from `Base` + active modifiers;
   clamp invariants inline (not via events).
5. Reaction systems — `Changed<Health>` death detection → `Dead` tag +
   `DeathEvent`; cue emission; etc.

## Effects

What the effect runtime guarantees today (`effects/EffectSystem.h`,
`effects/ActiveEffect.h`, `effects/EffectCost.h`):

- **Content declares the vocabulary.** `gameplay.attribute_declarations` assets
  declare attributes (name, range, default) and `effect.definition` assets declare
  effects (duration, period, modifiers by attribute name, granted tags by name).
  `DeclareContentVocabulary` registers tags, then attributes, then effects, each in
  asset path order, into a World's registries: the runtime when content is
  published, every editor document when it receives the asset stack. An effect is
  named by its asset path. A reloaded effect asset is redefined in place under the
  same id (`EffectRegistry::Define`); instances already applied keep the timing they
  were applied with. A module may still register effects and attributes in code;
  content adds to the same registries.

- **Time is whole fixed ticks.** `ApplyEffect` takes the tick it is applied on
  (`FixedSimTime`) and turns the authored seconds into ticks of that length, at
  least one. An effect counts down one tick per lifetime pass while its target is
  in an active partition, so a dormant target's effects pause rather than expire
  in its sleep. A duration of D ticks applied on tick N holds through tick N + D − 1
  and is gone for tick N + D; a period of P ticks first lands for tick N + P.
- **Storage.** Effect entities live in the persistent partition. A target that
  moves between zones keeps its effects, and its old zone detaching does not take
  them; an effect whose target is gone ends on the next lifetime pass, as does one
  whose owner is gone.
- **Owners, scale and removal.** `ApplyEffect` takes an optional `LeaseOwner`
  (an entity plus an opaque key) and a scale that multiplies every modifier's
  magnitude, whatever its op. Content applies effects through `effect.apply` and
  holds them through the `effect.hold` lease (`docs/gameplay/authored-api.md`). `RemoveEffect` ends one effect and
  `RemoveEffectsOwnedBy` ends everything an owner holds; neither may run inside a
  query.
- **Tags belong to the instance.** An effect records the tags it actually granted
  (up to `kEffectGrantedTagCapacity`; the registry refuses a definition granting
  more). Ending it revokes exactly those, whatever has become of the definition, and
  a tag the container could not hold is neither granted nor revoked.
- **The fold is canonical.** Floating-point folding depends on order, so modifiers
  fold per target in effect-definition order, then application order, never
  storage order.
- **Costs fail closed.** `CheckEffectCosts` decides whether an actor can pay several
  costs together: deltas are summed per attribute first, a Base may not go below
  zero or the attribute's minimum (whichever is higher), and a missing definition,
  attribute set or attribute, or a cost that is not an Instant Add, refuses.

## Ability definitions

An ability is content: an `ability.definition` data asset
(`abilities/AbilityDefinitionData.h`). Its full format and semantics are
`docs/plans/abilitykit-2.md` §3–§5 and §16; the normative example is
`test/fixtures/abilities/assets/abilities/charge_release.sdata`. In brief:

- **Activation, continuation, cancellation.** Three keyed condition lists: what
  must hold to start, what must keep holding, and what ends a running activation.
  A `target_lost` condition holds once the entity an `entity` or `hit` local named
  has ended; as a `cancel_on` entry it ends the activation `TargetLost`.
- **Phases.** Each has `enter` ops, an optional `while` pulse (`every` ticks),
  `hold` leases acquired on entry and released on exit, `exit` ops (run on a
  transition, never on a cancel), `present` cues, phase-scoped `continue`
  conditions and ordered `then` transitions (`on: release`, `on: timeout`,
  `on: complete`, or immediate; `to` a phase or `end`). `complete` waits until
  every hold of the phase that can finish on its own has (a lease whose
  definition `Completes`, as a timed `movement.burst` or a `movement.tether` does);
  a phase that holds none is `complete_unheld`.
- **Ops.** `invoke` a verb, `query` into a local, `set` a local, `commit` cost
  groups, emit a `cue`, or (in `hold`) a `lease`. Each may carry a `when` guard.
  Arguments come from a constant (the verb-binding forms: `const`, `tag`, `data`,
  `asset`, `entity`), a `port` (`actor`, `instigator`, `aim.origin`,
  `aim.direction`, `phase.progress`, `phase.ticks`, `pulse`, `time.scale`, and
  `target` inside an op with `targets`) or a `local` (with `field` on a hit).
- **Locals.** Up to eight slots of typed values for one activation (`bool`, `int`,
  `float`, `vec3`, `entity`; `hit` and `entities` take two; one `entities` local).
- **Costs and cooldown.** Cost groups paid at start or by a `commit`, once or per
  pulse; a cooldown effect started on start, on a group's commit, or on end.

Every element carries a stable key. Compiling happens in two stages, both
reporting `AbilityDiagnostic {Severity, Code, ElementKey, Path, Message}`:

1. `CompileAbilityDeclaration` reads the document with no World: field names,
   key uniqueness per kind, references between elements, reachability
   (`unreachable`, a warning), `no_exit`, `zero_time_cycle` over immediate
   transitions, cost and commit placement (`commit_repeat`, `commit_twice`,
   `cost_shape`), guards on values that may be empty or stale
   (`unguarded_maybe_empty`, `unguarded_recipient`; a guard counts as the op's
   `when` or a child of a top-level `all`) and capacities
   (`abilities/AbilityCapacity.h`). Loading refuses a document with an error.
2. Declaring it into a World resolves names: tags, attributes, effects, actions,
   verbs, queries, leases and component identities.

## Grants and input

An actor runs only what it is granted: `AbilityGrants` holds up to sixteen grants,
each an ability, a kind (authored on the actor, held by an effect, or made by a
request) and an entry in the World's `AbilityGrantEntries`. An entry is the
grant's stable key and its binding override; its effective binding is the
ability's `input_default` with every field the grant states replacing the
ability's, resolved against the action vocabulary input publishes
(`InputActionVocabulary`). The same ability is bound to different actions on two
pawns by two grants, never by a copy. Grant ids are minted per actor and never
reused, so revoking one grant ends exactly the activations it started. Scenes
save authored grants by key, ability asset and override
(`abilities/AbilityGrantsSerializer.h`); an ability a scene names before anything
declared it keeps its name.

A lasting effect may hold grants too: an `effect.definition`'s
`granted_abilities` lists grant entries in the same form (`key`, `ability` as
`{ "data": ... }`, optional `binding`), which is how an equipped weapon is an
Infinite effect granting its abilities. The pass grants them to the effect's target
the tick after the effect applies and revokes them the tick after it ends
(`AbilityEffectGrants`), so the activations they started end `GrantRevoked` on
every machine on the same tick.

An actor with grants carries `ActiveAbilities`, `AbilityLocals`, `ActorTime` and a
tag container, by derivation. It must also carry an `InputActionSourceRef` to read
input at all.

The pass walks the tick's ordered input edges (`docs/gameplay/input.md`, "Edges"):

- **Press A.** First, a running toggle started by A ends its hold (the press is
  its release). Otherwise the grants bound to A whose context matches are taken in
  priority order. Two exclusive bindings at the top priority start nothing
  (`BindingAmbiguous`). The top exclusive binding runs admission: admitted, it
  starts and consumes the press; refused, the press is consumed (`Blocked`) unless
  the binding says `when_blocked: fall_through`, which lets the next priority try.
  Observe bindings start whenever admitted and never consume.
- **Release A.** Reaches only the running activations A started, whatever the
  bindings say now. A `press_release` activation sees it as `on: release`; a
  `press` one ignores it; a `toggle` ignores a key coming up.
- **Interrupted release A.** Ends those activations `InputInterrupted`, unless the
  binding says `on_interrupt: release`.

`ability.activate`, `ability.cancel`, `ability.grant` and `ability.revoke` reach
the same admission by name, through `AbilityRequestQueue` (bounded; a full queue
answers `QueueFull`).

## Runtime

`AbilitySystem` runs once per fixed tick where the simulation is authoritative,
after input resolution and the peer command feed and after the actor clock
advances, in five steps over every actor with grants:

1. **Sweep:** the definition still current, the grant still held, the input source
   unchanged (`InputLost`), continuation and phase `continue` conditions
   (`ContinuationFailed`), `cancel_on` (`CancelTriggered`, or `TargetLost` for a
   `target_lost` entry).
2. **Input:** the edges above.
3. **Requests:** the queue, in order.
4. **Advance:** pulses that fell due, then timeouts, completions and condition
   transitions. A completion is seen the tick after the hold finished, since the
   movement systems that step the hold run after the pass.
5. **Linger:** ended records past `ability.present_linger_ticks` are freed.

Every start runs to quiescence before the next edge: enter the phase (acquire its
holds, run its `enter` ops, the first pulse), then take the first transition whose
trigger and guard hold, run the old phase's `exit` ops and release its holds, enter
the next, and so on until it waits or ends. More than `ability.step_budget`
transitions in one tick ends it `StepBudget`. So three presses of an instant
ability in one tick are three activations. An activation's key is the command
tick, the ability, and the press's place in the tick's edges (or the request's
place in the queue): two machines that admit differently still name the same
press the same way.

**Admission** checks the definition, the activation conditions, `instances`, the
four live records, and that the start costs and an `on_start` cooldown can be
paid. **End** is the only exit and is idempotent: holds are released newest first,
owned tags revoked, an `on_end` cooldown started, exit ops never run; the record
lingers for presentation with its reason and the element behind it.

**Commit** pays cost groups atomically: every check (definitions, Instant Add
shape, attribute floors summed per attribute, the cooldown's tag room) passes
before anything applies, or nothing applies and the activation ends `CommitFailed`
(unless the op says `on_fail: continue`). A `once` group already paid is a traced
no-op. A cooldown of D ticks started on tick N blocks ticks N … N+D−1 and admits on
N+D.

**Ops** run against the dispatchers the host gives the kit (`AbilityHost`): an
`invoke` or a hold through `VerbDispatcher` with the actor as producer and the
activation's instigator; a hold's owner is the actor and a key of the record and
the op, so a provider ends exactly it. A `query` writes its local, or empties it
when the query has no answer. Guards are evaluated at the op; a stale entity skips
the op closed. Phases measure time on the actor's clock unless they say
`simulation`.

`AbilityTrace` records starts, refusals with their reason and failing condition,
transitions, ends, skipped ops, operation admissions, cues, ambiguous and blocked
presses. A warm pass allocates nothing.

## Presentation

An ability says what it shows by naming cues; a `cue.presentation` asset says how
each cue is shown. The ability's simulation never runs a presentation, and a
presentation can never change the simulation: the library keeps only
cosmetic-scope operations (`scope_mismatch` otherwise), `once` entries must be
verbs and `sustain` entries leases. Bindings read the ports `actor`, `target`,
`point`, `normal`, `seed` and `count`. Several assets may present the same cue;
their entries run in path order.

Three channels carry what happened, all in replicated state:

- **Discrete cues** — a `cue` op outside a pulse appends to the actor's
  `AbilityCueLog`, a ring of eight entries with a u16 serial, the activation, the
  op, the tick and the place (a hit's point, normal and entity, an entity, or a
  vector).
- **Pulse cues** — a `cue` op in a pulse list writes nothing new: the record's pulse
  count and its last hit (`LastPoint`, `LastNormal`, `LastTarget`) carry it, so an
  automatic weapon costs no log entries.
- **Sustained presentation** — a phase's `present` entries are held as leases while
  that phase is live, and released when it ends or the record goes.

`AbilityPresentationSystem` runs only where something presents (`AbilityHost::
Presents`, set when the engine renders). It keeps a machine-local
`AbilityPresentationState` per actor, never replicated or journaled. The first
time it sees an actor it takes what the actor already shows as presented, except
discrete cues younger than `kAbilityCueWindowTicks`, so a late joiner neither
replays an old fight nor misses the shot that was just fired. After that it
presents each new log entry once, each record's new pulses as one occurrence with
`count` (at most `kAbilityPulsesPerOccurrence`), and holds or releases a record's
sustained leases as its phase changes. A log overrun is counted in
`Overwritten`, and the cues lost to it are not presented.

## Replication

What presents an ability travels to every machine that sees the actor:
`ActiveAbilities` and `AbilityCueLog` have codecs (`ActiveAbilitiesWire.h`,
`AbilityCueLogWire.h`). A record sends its activation key, start order, state,
phase and its clock, end reason and element, pulse count and last hit; grants,
leases, owned tags and the trigger stay with the authority. Ids cross by name: an
ability by `AbilityRegistry::WireKey` (its path's `NameWireKey`), a tag by
`WireTag`, an entity as a wire entity. Each record is split into runs by how often
they change, so a pulse resends only its count and hit, and the hit is sent at
`kAbilityHitPointBits` and `kAbilityHitNormalBits` (about three centimetres and a
degree). A receiving machine gets the cue log and presentation state with the
records (`ActiveAbilities` derives both), and presents them with the same
`AbilityPresentationSystem` and the same first-sight rule.

The loss bounds, each pinned by `test/runtime/AbilityReplicationTests.cpp`:

- a record that began and ended between publishes still presents, through its
  linger (`LingerTicks`, four publishes at the defaults);
- a record's last pulses are lost only if every publish of its linger is;
- a discrete cue is lost only if eight newer ones are logged before a publish
  that carries it arrives;
- an older state applied after a newer one presents nothing again, and a log's
  serial wraps without losing order.

One firing actor costs a peer 344 B/s at the default cadence with its aim
sweeping, every pulse landing somewhere new (`CueBandwidthPerFiringActorIsMeasured`
fails above 360).

## Rollout

Each stage is independently testable and buildable with physics/animation
stubbed. Stages 1–5 are the spine; 6 proves it end to end; 7 is deferred.

### Stage 1 — Tags in the ECS

First, relocate the gameplay tags module out of `engine/.../core/gameplay_tags/`
into the `framework/` area, and move its tests to `test/framework/` (D-J).
Then: POD `GameplayTagContainer` component; `GameplayTagRegistry` as a world
resource; register both; grant/revoke-with-stacks API; hierarchical queries via
the registry; name↔id serialization codec.

Gate: a test grants/revokes/stacks tags on an entity, matches a hierarchical
`GameplayTagQuery`, and round-trips a scene with tags by name.

### Stage 2 — Attributes

`AttributeId` + `AttributeRegistry` resource (data-defined attributes); POD
`AttributeSet` component; `AttributeResolveSystem` doing Base→Current + clamp
(no modifiers yet).

Gate: define attributes in data, set/read base, observe clamped current; scene
round-trip by name.

### Stage 3 — Effects

Effect-def data asset; `EffectApplicationSystem` (instant→Base, duration→spawn
`ActiveEffect`); `EffectLifetimeSystem` (durations, periodic, expiry, granted
tags); modifier folding in `AttributeResolveSystem`.

Gate: instant damage lowers Health; a duration buff raises Current then expires;
stacking respects the policy; a granted tag appears for the effect's lifetime and
is revoked on expiry.

### Stage 4 — Abilities

Ability-def data asset; `AbilitySet` component; `Intent` plumbing;
`AbilityActivationSystem` (require/block via tags, cost, cooldown-as-effect,
granted tags, cancel-by-tag).

Gate: activation is gated by require/block tags; cost is paid; cooldown tag
blocks reactivation until expiry; two abilities that block each other cannot be
co-active.

### Stage 5 — Reactions & events

`Events<T>` queue resource; death detection (`Changed<Health>` → `Dead` +
`DeathEvent`); gameplay-event routing; `ICueSink` (stub) for cosmetic cues.

Gate: Health ≤ 0 yields a `Dead` tag and one `DeathEvent`; a downstream system
consumes the event exactly once; a cue is emitted to the stub sink.

### Stage 6 — Edges + worked vertical slice

Thin sinks (`IImpulseSink`, `IMontageSink`, `IHitQuery`, `ICueSink`) with stub
implementations. Two authored abilities exercising the whole spine:

- **Dash**: costs Stamina, grants `State.Invulnerable` for a duration (i-frames),
  blocked while `State.Stunned`, applies an impulse via the stub sink.
- **Fireball**: costs Stamina, applies a cooldown, spawns a projectile (or stubs
  it), and on hit applies a periodic `Burning` duration effect that grants
  `State.Burning`.

Gate: both abilities authored entirely as data; tests drive activation and assert
attribute/tag/effect outcomes against the stub sinks.

### Stage 7 — Deferred

Ability *tasks* for channeled/charged/combo abilities (an active-ability-instance
entity with a phase/timer cursor — a cursor, not an HSM); active-effect
persistence via the streaming overlay; meta-attributes (transient Damage input)
if execution calculations demand them.

## Open Questions

- **Hot-attribute promotion.** Keep all attributes in the `AttributeSet` block,
  or promote a few hot ones (Health) to dedicated components for
  `Query<Read<Health>>` ergonomics — at the cost of two sources of truth? Lean:
  block is canonical; promotion is an optional, documented optimization.
- **Capacity caps.** `GameplayTagContainer::Cap`, `AttributeSet::Cap`,
  `AbilitySet` size. Proposed 32 / 16 / 16; tune from real content. Overflow
  policy (assert vs drop vs spill) TBD.
- **Effect ordering/determinism.** Application order within a frame, same-frame
  stacking, cost-before-or-after-behavior. Needs an explicit rule.
- **Cooldown representation.** Cooldown-as-effect-granting-a-tag (this plan) vs a
  dedicated cooldown component. Lean: effect+tag, for uniformity.
- **Intent home.** Queue resource vs per-entity intent component; which frame
  phase produces intents.
- **Meta-attributes.** Model GAS-style transient Damage/Healing inputs, or apply
  effects directly to real attributes? Lean: direct, until execution calcs need
  otherwise (Stage 7).

## Architectural Smells

Be suspicious when new code:

- introduces a per-entity "ability system object" instead of components+systems
- adds an HSM to arbitrate abilities
- registers an attribute-changed callback / observer
- stores a `std::vector` of effects (or tags) inside a component instead of using
  effect entities / the POD container
- adds or removes a *transient* event/flag component every frame (archetype churn)
- serializes raw tag/attribute ids into a scene instead of names
- has ability behavior call physics/animation/render/audio directly instead of
  through an AbilityKit-owned sink
- makes the heap-backed `GameplayTagSet`/`CountedGameplayTagSet` a component
- lets `framework/` code include `render/`/`graphics/`/scene headers, or lets
  engine `core/`/`render` include `framework/` — either direction breaks the
  decoupling
