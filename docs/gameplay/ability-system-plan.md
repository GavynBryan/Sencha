# Sencha Gameplay Ability System Plan

Status: **proposed working plan** (2026-06-24).

This document plans a Gameplay Ability System (GAS) for Sencha, built the
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
  cues integrate through thin sinks the GAS owns (D-J); they are stubbed until
  those systems exist, so the GAS is buildable in parallel.
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

`AbilitySet`
: A POD component listing the abilities an entity has been granted (by handle).

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
  target by `EntityId` and carrying resolved modifiers, remaining time, stacks,
  and granted tags.
- `EffectLifetimeSystem` ticks time, applies periodic ticks, expires/despawns via
  `CommandBuffer`, and grants/revokes tags on the target container.
- `AttributeResolveSystem` recomputes each target's `Current` from `Base` plus all
  active modifiers, then clamps to the attribute's range.

Effects-as-entities batch and churn like projectiles, so they use the same
deferred-spawn path. Effect *definitions* are data assets referenced by handle.

### D-E — Abilities are data + one activation system; cost/cooldown are effects

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

### D-G — Behavior reaches the world through thin sinks the GAS owns

An ability's effects on attributes and tags are pure data. Its effects on the
*world* — impulse/knockback, montage + hit-window timing, overlap/trace hit
detection, cosmetic cues — go through narrow interfaces (`IImpulseSink`,
`IMontageSink`, `IHitQuery`, `ICueSink`) that the GAS defines and that physics /
animation / render / audio implement later. Stubs now; real implementations when
those systems land. The GAS must not reach into those backends directly.

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

### D-J — GAS lives in a delineated `framework/` area, decoupled by rule, not a separate library

Gameplay is not core engine behavior, but the separation that matters is a
*dependency rule*, not a build target. All GAS code — gameplay tags, attributes,
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

1. `AbilityActivationSystem` — intents → cost/cooldown effects applied, tags
   granted, behaviors fired.
2. `EffectApplicationSystem` — drain apply-effect requests: instant → `Base`,
   duration → spawn `ActiveEffect`.
3. `EffectLifetimeSystem` — tick durations, periodic ticks, expiry, tag
   grant/revoke.
4. `AttributeResolveSystem` — recompute `Current` from `Base` + active modifiers;
   clamp invariants inline (not via events).
5. Reaction systems — `Changed<Health>` death detection → `Dead` tag +
   `DeathEvent`; cue emission; etc.

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
  through a GAS-owned sink
- makes the heap-backed `GameplayTagSet`/`CountedGameplayTagSet` a component
- lets `framework/` code include `render/`/`graphics/`/scene headers, or lets
  engine `core/`/`render` include `framework/` — either direction breaks the
  decoupling
