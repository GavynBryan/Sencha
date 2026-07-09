# Ability Architecture: Activation Lifecycle, Payload Ops, and Ability Assets

Status: **proposed working plan** (2026-07-09). Written against the ability
architecture spec (activation gating, effect representation, held/mode
abilities, editor authoring). Extends `docs/gameplay/abilitykit.md` (decisions
D-A through D-J) and resolves the questions that document left open: what an
ability "behavior" is, and how channeled/charged/held abilities work (its
deferred Stage 7). Decision ids here continue that series at D-K. Roadmap
reconciliation notes are in section 6.8; the roadmap owns versions and
priorities.

Everything in this document was checked against the tree at the stated date.
Where the spec and the tree disagree, the tree is cited by path.

---

## 1. Evidence base: what the tree already contains

The spec presents "what happens when an ability fires" as an open green-field
choice. Most of the substrate landed between 2026-06-30 and 2026-07-01 and runs
in the template game today:

- **Gating (spec part a) is implemented.** `TryActivateAbility`
  (`engine/src/abilities/AbilitySystem.cpp`) checks grant membership
  (`AbilitySet`, POD component, capacity 16), a `GameplayTagQuery`
  (All/Any/None, Exact/Hierarchical) against the actor's
  `GameplayTagContainer` (POD, capacity 32, ref-counted stacks), and cost
  affordability against `AttributeSet` Base values. Cooldown is a Duration
  effect granting a tag that the query blocks on. Intents
  (`AbilityActivation { Actor, Ability }`) arrive through a queue resource so
  player input and AI share one path.
- **Effects are entities.** Duration/Infinite effects spawn an `ActiveEffect`
  entity referencing the target; expiry revokes granted tags (ref-counted) and
  destroys the entity. Instant effects modify attribute Base once. Periodic
  effects reapply on a timer. (`engine/include/effects/`)
- **Movement modes are the mode architecture the spec asks about.** Locomotion
  modes are zero-size marker components (`OnGround`, `InAir`, plus any a game
  registers), exactly one per character, swapped only by a priority-arbitrated
  single writer (`ApplyLocomotionModes`), each projecting a gameplay tag
  (`movement.grounded`, `movement.airborne`) that abilities gate on.
  (`engine/include/movement/LocomotionMode.h`, `MovementModes.h`)
- **Jump is the worked end-to-end example.** The jump ability is authored data:
  gate requires `movement.grounded` (hierarchical) and blocks
  `movement.jump.cooldown`; `OnActivate` applies a 0.05 s effect granting the
  one-tick `movement.jump.requested` tag; `JumpExecutionSystem` consumes that
  tag into `CharacterController.PendingJumpSpeed`, and the physics mover
  applies it. Per-tick order is explicit: grounding transitions, mode arbiter,
  ability activation, jump execution, attribute resolve, locomotion, effect
  lifetime, then physics. Input to impulse resolves in the same fixed tick.
  (`engine/src/movement/MovementRegistration.cpp`)
- **The data/editor machinery exists for assets, not for abilities.** Stable
  asset identity (`AssetId`, cook-minted, `asset_ids.json`), staged
  `IAssetLoader`, content-hashed cook cache, hot reload with handle-stable
  in-place swap, `TypeSchema`/`RuntimeSchema` reflection, and kyusu's
  schema-driven inspector with an asset picker over `AssetRegistry` records.
  Ability and effect definitions do not participate yet: they are registered
  from C++ (`RegisterDefaultMovementAbilities`), have no asset type, no
  serializer, and no editor surface.

What is genuinely missing, and is therefore the actual scope of this plan:

1. **Activation lifecycle**: press/hold/release/toggle policies, held-time
   tracking, repeat-while-held, and cancellation semantics. Today every
   activation is fire-and-forget.
2. **Parameterized world verbs**: an ability can only apply effects (attribute
   modifiers plus granted tags). Verbs with payload (fire a projectile scaled
   by charge time, set a time scale) have no channel.
3. **Ability definitions as assets**: `.sability`/`.seffect` authored data with
   stable identity, cook, hot reload, scene references, and an authoring
   surface.
4. **Serialization wiring**: `RegisterGameplayTagSerializer` and
   `RegisterAttributeSerializer` exist but are never called; `AbilitySet` has
   no serializer at all. Grants currently do not survive a scene round trip.

---

## 2. Objections to the spec

Minimum five requested; nine found. Tags: WRONG (incorrect as specified),
UNDERSPECIFIED (needs a decision), TASTE (works, better shape exists).

**O1. WRONG: the spec is written as green-field, but the contested part is
half-landed and shipping.** Spec section 5 offers options A through D as
competing architectures; the tree already contains the hybrid: data-authored
gate plus effects (option B's spine), capability systems consuming deposited
tags (option A's systems, keyed by mechanism, not by ability name), and native
code behind registered names (option D's registration). Only option C (a
statechart runtime) is a genuine fork. Evaluating A through D as alternatives
would produce a plan that demolishes working code; the honest deliverable is a
delta against the landed substrate, which is what section 5 of this document
evaluates and section 6 specifies.

**O2. WRONG: the "no scripting VM" constraint contradicts the standing
roadmap.** The spec: "There is no scripting VM ... Do not design around a VM."
`docs/plans/engine-roadmap.md` (standing master plan, owner decision dated
2026-07-02) schedules an embedded Lua-family scripting runtime for v1.0, with
integration explicitly through CommandBuffer, tags, AbilityKit intents, and
reflection. Both documents cannot stand. This plan needs no VM and does not
design around one, and the seams it defines (the intent queue and the op
registry, D-L) are exactly the surface a script would call if the roadmap item
survives, so the architecture is unaffected either way. But the owner should
reconcile the two documents on the record.

**O3. WRONG: spec 6.1's "container of active effects on the owner
(`List<ActiveEffect>`)" is not implementable as specified.** Components must be
trivially copyable (`static_assert` in `World::RegisterComponent`,
`engine/include/ecs/World.h`); heap containers inside components are also on
abilitykit.md's smell list. The real options are a fixed-capacity POD row
array or separate entities, and effects-as-entities already landed. The
spec's option list should have said so; as written it invites a design that
fails at the first compile. (The spec's "component-type budget of about 256,
likely `array<u64,4>`" guess is confirmed: `std::bitset<256>` over
`MaxComponents = 256`, asserted at registration. The landed kit spends 4 of
those types for all of tags, attributes, effects, and grants; content never
touches the budget. This plan adds 2 more.)

**O4. WRONG: spec 3's lean toward component-presence conditions inverts the
landed design, and its trigger for adopting tags is backwards.** The spec leans
"component presence for simple boolean states, tags only if a family-query
taxonomy appears." Three problems. First, stacking: two independent stun
sources need ref-counting, which `GameplayTagContainer` grants/revokes provide
and component presence cannot (who removes the component when one of two stuns
expires?). Stacking, not taxonomy, is the deciding feature, and cooldowns
already rely on it. Second, budget: a component type per blockable boolean
spends the scarce resource this spec itself protects. Third, the hierarchy
trigger has already fired: the landed jump gate matches `movement.grounded`
hierarchically so any grounded substate qualifies. Markers do remain correct
for one thing: archetype-level system dispatch, which is exactly how locomotion
modes use them (`MovementModes.h`: "Mode lives in archetype membership, not a
tag-container value"). The rule the tree already embodies: **tags for
queryable state values, markers for which system runs.** This plan keeps it
(D-P).

**O5. UNDERSPECIFIED: input lifecycle is the real contested ground, and the
spec files it under the wrong heading.** Charge, automatic, glide, cling, and
mist are activation-lifecycle shapes (what press, hold, release, and toggle
mean), not effect-representation shapes, yet spec 2 files them under "what
happens when it fires." The undecided questions: the edge vocabulary of
intents, who tracks held state and repeat cadence, what cancellation means
mechanically (which enders exist, what cleanup runs for each), and input
buffering (the engine has none; `InputFrame` edges are drained on the first
fixed tick of a frame and persist across zero-tick frames, nothing more).
Resolved here as D-K; buffering is assigned to the roadmap's input action
mapping item, with the intent contract it must emit defined in D-K.

**O6. UNDERSPECIFIED: "stable id" collides with three identity systems the
spec does not acknowledge.** The tree has `AssetId` (u64, cook-minted, stable
across renames), runtime definition ids (`AbilityId`/`EffectId`, u32,
registration-order, explicitly unstable across builds), and per-World registry
scope: every zone registry owns its own `World` with its own `AbilityRegistry`,
so a runtime id is not even stable across zones in one process. "Kyusu
references an ability by id" must mean the asset identity (id-first,
path-fallback `AssetFieldRef`, the existing picker convention), the canonical
cross-world identity must be the registered name (the tag/attribute
serialization precedent), and runtime ids must never be serialized. Resolved
as D-M.

**O7. UNDERSPECIFIED: the dedicated ability editor is asserted as a given, but
no such decision exists in the repo, and the schema-transport question it
implies is unaddressed.** abilitykit.md lists "No authoring front-end" as a
non-goal; the roadmap schedules no ability editor. Meanwhile the spec's own
requirement, "native code exposed to either editor via a registered string id
plus parameter schema," needs a transport: game-registered ops live in the
game module, and editors are separate executables. The tree already answers
this (editors load the game module DLL via the landed `GameModuleLoader` and
ABI fingerprint, the same way kyusu gets game components today) but somebody
has to say so. Resolved as D-Q, including the recommendation to sequence the
editor after the asset layer proves itself in text form, since hot reload of
authored JSON already exists.

**O8. TASTE: the hookshot stress test biases the evaluation toward sequencing
machinery, and the pass criterion should be decomposition.** The spec half
concedes this ("its one truly sequenced part was cleaner as an ordinary
system"). A design that expresses the hookshot as ordinary mechanisms (a mode,
a projectile entity, translator systems) without any ability-side sequencer is
a pass, not a dodge. Building interpreter machinery for it would also
contradict the repo's own deferral doctrine: the roadmap declines seams until
the second consumer exists (`IPoseModifier`, `IZonePopulationStrategy`), and
there is no first consumer of ability sequencing in either game. Section 7
shows the decomposition; section 8 records the tripwire that would reopen
option C.

**O9. WRONG in premise, TASTE in placement: "slow time" is listed as an
ability, but the engine has no time dilation to toggle.** `SimulationTimescale`
is a pause gate: zero emits no ticks, any positive value emits exactly one tick
per frame with `FixedSimTime::DeltaSeconds` unchanged
(`engine/src/runtime/RuntimeFrameLoop.cpp`). Fractional slow motion does not
exist. The ability itself is trivial once the mechanism exists (a Toggle-style
ability whose ops call the time service, D-R), but the mechanism is frame-loop
work with determinism consequences, and scoping it into the ability
architecture would misplace it. It is called out as a dependency, not designed
here.

---

## 3. Option evaluation (spec 5 against spec 8)

Judged with the landed substrate as context. Criteria: (1) data-driven,
(2) data-oriented, (3) easy to reason about, (4) right-sized, (5) extensible,
(6) composable with movement.

**A. Bespoke native code per ability.** Strong on 2, 3-per-ability, 6; fails
1 outright (new ability requires an engineer and a rebuild; the ability editor
requirement is unmeetable), and decays on 3-in-aggregate as N systems re-roll
gate/cooldown/held logic with drifting conventions. The tree already
demonstrates the correct residue of A: systems named for mechanisms
(`JumpExecutionSystem`, `GroundLocomotionSystem`), never for abilities, owning
the durational work that data deposits. A survives as the capability-system
layer, not as the ability representation.

**B. Data-authored ability plus flat effect list.** This is the landed shape
extended. Scores top marks on 1 (definitions become assets, D-M), on 2 (rows
in POD containers, effects as entities, four component types today), and on 6
(the tag seam into locomotion modes is already how jump works). Its two real
weaknesses: a flat list cannot express input lifecycle (fixed by D-K, which is
activation-side, not list-side) and cannot carry parameterized world verbs
(fixed by D-L's op registry). The spec's stated worry, "leans on
transient/marker components," turns out not to apply: the landed idiom carries
one-tick signals as tag values inside a persistent container, payload requests
as slots on persistent components (`PendingJumpSpeed`), and duration as effect
entities. No per-frame add/remove anywhere. See D-N for the full ruling.

**C. Data statechart / generic flow runtime.** Honest case for: uniform
authoring of temporal logic, one inspectable runtime, and the only option that
makes hookshot-class abilities pure data. Honest case against, on the
criteria: 4 is a hard fail (zero shipping abilities in either game need a
program counter; every section-4 ability decomposes into lifecycle buckets,
effects, and modes); 3 degrades (two-layer debugging: the data program plus
the interpreter, and designers can now author unanticipated control flow);
2 is mediocre (per-activation cursor-plus-bindings state either fattens rows
or spawns bookkeeping entities); 5 converges on B anyway (new step kinds need
a registered-op vocabulary, at which point the statechart is an op list with
gotos). It is also a small interpreter, the thing the owners already killed
once at language scale. Rejected now, with a tripwire (section 8) and a
compatibility note: if it is ever built, it should be a data upgrade of the
same asset (states referencing the same gate/ops vocabulary) so nothing in
this plan is throwaway.

**D. Native script/behavior hatch.** As the common path it fails 1 (logic in
code, editor authors parameters only). As the escape hatch it is correct, and
this plan folds it into D-L rather than keeping a separate per-ability functor
concept: a "hatch" ability is an ability whose op is game-registered native
code, usually paired with a game component and a game system. Config stays
data (the op's params schema), lifecycle stays uniform (the same rows and end
reasons as every other ability), and there is no second registration concept
to learn.

**Recommendation: B extended by D-K/D-L/D-M, with A as the consumer layer and
D folded into the op registry. C rejected with a tripwire.** Priority order
where criteria conflicted: right-sized (4) and data-oriented (2) were weighted
above generality, because the section-4 ability inventory is the contract and
the repo's own doctrine defers seams until a second consumer exists.
Data-driven (1) is satisfied at the asset layer rather than by runtime
machinery.

---

## 4. Resolved questions (spec 6)

Each resolution is a decision record continuing abilitykit.md's series.

### D-K. Activation lifecycle: styles, edges, rows, end reasons (spec 6.1 input half, 6.3, 6.4 ability side)

**Decision.** Intents gain an edge: `AbilityActivation { Actor, Ability, Edge }`
with `Edge in { Press, Release }`. `AbilityDefinition` gains an
`ActivationStyle in { Instant, WhileHeld, Toggle }`, an optional `WhileActive`
effect reference (must be Infinite; applied on start, ended on stop; its
granted tags and modifiers ARE the held state), an optional `InterruptedBy`
tag query, and an optional tick period. A new POD component, `ActiveAbilities`
(capacity 8 rows: `{ AbilityId, EntityId WhileActiveEffect, uint64 StartTick,
uint64 NextTickIndex }`), tracks live WhileHeld/Toggle activations on the
actor. It is registered and added alongside `AbilitySet` so steady-state play
never performs a structural change for activation bookkeeping.

Per fixed tick, the activation system (extending the landed
`AbilityActivationSystem`) runs three passes in this order, all deterministic
(rows in array order, entities in chunk order, queue FIFO in push order):

1. **Row maintenance.** For each row: end with reason `Stripped` if the
   ability is no longer granted; end with reason `External` if the
   `WhileActiveEffect` entity is dead (someone ended the effect from outside:
   this is the reverse channel, no callbacks); end with reason `Interrupted`
   if `InterruptedBy` matches the actor's tags.
2. **Queue drain.** `Press`: Instant activates exactly as today; WhileHeld
   activates and opens a row (a second press while a row exists is ignored);
   Toggle opens a row or, if one exists, ends it with reason `ToggledOff`.
   `Release`: ends the matching WhileHeld row with reason `Released`; no-op
   otherwise.
3. **Ticks.** Rows whose definition has a tick period and whose
   `NextTickIndex` has arrived re-check the activation requirements and cost,
   pay the cost, and run the tick ops. A failed re-check ends the row with
   reason `Interrupted` (an automatic weapon stops when ammo or a required tag
   runs out).

Ending a row always destroys its `WhileActiveEffect` entity through the effect
system's end path (revoking its granted tags, ref-counted). Two op buckets
distinguish payload from cleanup: `OnReleaseOps` run only for `Released` and
`ToggledOff` (a charge shot fires on release, not on stun), `OnEndOps` run for
every end reason (a global state toggled on must always toggle off). Held time
is derived, never accumulated: `HeldSeconds = (TickIndex - StartTick) *
FixedDt`, available to ops through the op context.

**Rationale.** This is the smallest structure that covers every held/toggle
ability in the section-4 inventory (charge, automatic, glide, cling, mist)
without an interpreter: no program counter, no wait index, no binding
resolver, just lifecycle buckets keyed by an enum plus a fixed-cap row array.
The row poll is over tens of entities with at most 8 rows each; no event
plumbing is warranted. Cost and cooldown stay activation-time (landed
semantics); an ability that wants a cooldown-from-release applies a cooldown
effect in its `OnReleaseOps`, which composes instead of adding a mode switch.

**Alternative considered.** Representing held state purely as the
`WhileActive` effect with no row (release revokes by tag). Rejected: release
routing needs to find *which* effect entity to end, held time needs a start
tick, and autofire needs a next-fire cursor; the row is exactly that state and
nothing else. Also considered ability tasks as entities (abilitykit.md Stage 7
sketch): rejected below in D-N.

### D-L. Payload ops: registered verbs with schemas (spec 6.2, 6.3)

**Decision.** A world resource `AbilityOpRegistry` maps a dotted op name to
`{ native function, params TypeSchema }`. Op params are one POD struct per op,
described by the same `TypeSchema`/`RuntimeSchema` machinery components use,
which gives JSON codec and editor property-grid rendering for free. The
definition's payload buckets (`OnActivateOps`, `OnReleaseOps`, `OnTickOps`,
`OnEndOps`) are short ordered lists of `{ op id, params blob }`. Ops receive
`AbilityOpContext { World&, actor, ability, TickIndex, FixedDt, HeldSeconds,
EndReason }`. Ops run outside query iteration (the activation system already
does) and may perform structural changes under the same rules as
`TryActivateAbility` today.

The engine ships exactly one built-in op: `sencha.apply_effect` (apply an
effect reference to the actor). The existing `OnActivate` effect field keeps
working and is equivalent to a one-op list. Everything else is game-registered:
`game.ctx_fire`, `game.time_scale`, `game.ui_open`. The intended
implementation shape for a world verb is the landed request-slot pattern
generalized: the op validates and writes a request (a slot on a persistent
component, the way `CharacterController.PendingJumpSpeed` works, or a granted
request tag), and a mechanism-named game system consumes it later in the tick.
Ops that need per-actor tuning should read it from components/attributes (the
way jump speed lives on `MovementProfile`), keeping ability assets free of
per-character numbers.

Dispatch ruling (spec 6.2): **two-level, both already idiomatic.**
Payload-less causes are granted tags consumed by translator systems (landed:
jump). Payload causes are registered ops writing request slots (new, but a
generalization of `PendingJumpSpeed`). The registry is central only for
*naming and schemas* (so the editor can enumerate and render authoring UI);
*ownership of consumption* stays decentralized in the systems that own the
mechanism. A god-switch cannot form because the registry holds opaque
functions, not cases.

Span versus instant (spec 6.3): one authoring surface, two execution
behaviors, chosen by the data itself. Instant work runs inline in the
activation pass; durational work is deposited (an effect entity, a granted
tag, a request slot) and owned by the system named for the capability. The
spec's worry that deposits cost a frame of latency is disproven by the landed
schedule: activation is explicitly ordered before the consumers inside the
same fixed tick (arbiter, then activation, then jump execution, then
locomotion, then physics), so a deposit made this tick acts this tick. That
ordering contract becomes a documented invariant: **ability activation runs
after mode arbitration and before verb-consuming systems within the fixed
tick.**

**Rationale.** The op registry is the narrowest seam that satisfies three
requirements at once: the editor's "string id plus parameter schema" contract,
the gameplay engineer's "add a new kind of behavior without engine edits"
contract (D-Q shows both), and the charge shot's "payload derived at runtime"
requirement (`HeldSeconds` scaling). It also subsumes the roadmap's planned
`IImpulseSink`/`IMontageSink`/`IHitQuery`/`ICueSink` quartet, which was never
built: four fixed interfaces would hardcode the verb vocabulary in the engine,
while ops keep it game-owned and data-visible. Section 6.8 proposes the
roadmap edit.

**Alternative considered.** Growing more effect kinds (an `EffectDefinition`
with an impulse field, a spawn field, ...). Rejected: effects are a clean
closed mechanism over attributes and tags; verb fields would turn the effect
struct into a junk drawer and drag world knowledge into the effect system.
Also considered per-verb bespoke translator systems with no registry (pure
jump pattern everywhere). Rejected only as the *complete* answer: it fails
runtime-derived payloads (charge) and gives the editor nothing to enumerate;
it remains the preferred implementation *inside* ops.

### D-M. Definitions become assets; identity bridges by name (spec 6.6a, editor requirement)

**Decision.** Two authored JSON asset types, following the `.smat` conventions
exactly (versioned, unknown keys are errors, defaults omitted on write, pure
parser/writer, no importer, hot reload by virtual path):

- **`.seffect`**: one `EffectDefinition` plus its registered name. Referenced
  from abilities and from game code; shareable (a slow effect authored once
  serves a trap, an enemy, and an ability).
- **`.sability`**: one ability: registered name, activation style, gate
  (require/block tag names with match modes), cost/cooldown/while-active
  effect references (as `asset://` refs), interrupt query, tick period, op
  buckets (op name plus params object, validated against the op's schema).

Identity bridge, resolving O6: the **asset id** (`AssetId`, cook-minted) is
the authoring and storage identity that editors reference; the **registered
name** inside the asset is the canonical cross-world runtime identity (the
tag/attribute precedent); the **runtime id** (`AbilityId`, per-World) is
resolved at install time and never serialized. Loading: staged loaders parse
into definition caches (handle-stable, hot-reloadable); an install step
registers every loaded definition into a `World`'s registries at world/zone
setup (where `RegisterMovement` runs today), resolving tag and attribute names
through that world's registries (auto-creating tags, the landed behavior).
Hot reload re-installs changed definitions into live worlds, which requires
`AbilityRegistry`/`EffectRegistry` to gain `Update(name, def)` (same id, new
payload); today re-registration keeps the first definition, which would make
hot reload silently inert. Effect references from abilities are `asset://`
paths, so the existing manifest walk (`CollectAssetPaths`) sees them and
preloading works; name-only references would be invisible to it.

Scene references: a new authored component `GrantedAbilities` (fixed-cap array
of ability asset handles, `TypeSchema` fields tagged `.AsAsset(Ability,
List)`) is what kyusu edits; the existing picker UI then works with zero
editor code. On load, a resolve step grants the referenced abilities into
`AbilitySet` (and ensures `ActiveAbilities`). `AbilitySet` additionally gets a
by-name serializer (the tag-container precedent) so runtime-acquired grants
survive a scene round trip. Stage 0 also wires the existing tag and attribute
serializers, which are currently defined but never installed.

**Rationale.** This is the shortest path to the spec's actual requirement (a
standalone, independently authored, id-referenceable ability asset) that
reuses every landed mechanism: asset identity, staged load, hot reload,
reflection, picker. It also keeps tests and code-first workflows intact:
`RegisterDefaultMovementAbilities` keeps working; assets are another source of
registrations, not a replacement.

**Alternative considered.** Bypassing per-World registries and letting
`AbilitySet` hold asset handles directly. Rejected: per-World registries are
deliberate (zone isolation, headless tests that author definitions in-test,
code-registered defaults like `movement.jump`), and gate evaluation wants the
definition resolved once per world, not a cache hop per activation. Also
considered inlining effects inside `.sability`. Rejected for reuse and for
manifest visibility; a convenience inline form can be added later without
breaking the format.

### D-N. Where "ability in progress" lives (spec 6.1)

**Decision.** There is no single representation; there are four homes chosen
by who owns the lifecycle, and every section-4 ability maps onto them:

1. **Nothing.** Instant abilities leave no trace beyond their effects. Jump
   is already this.
2. **Tags via effect entities** for gate-visible state with duration:
   cooldowns, i-frames, "charging" as a queryable fact, the double-jump used
   gate. Landed mechanism, ref-counted, batch-expired.
3. **`ActiveAbilities` rows** (D-K) for input lifecycle only: which held or
   toggled activations are live, since when, when they tick next. Fixed-cap
   POD, runtime-only, never serialized.
4. **Locomotion mode markers plus per-mechanism state components** for world
   mechanics: dashing, gliding, clinging, misted. Owned by movement, entered
   and exited only through the arbiter, projecting tags for everyone else.
   A mode that needs parameters (dash direction) carries a small POD state
   component written at entry, the landed `MovementState` pattern. Effects
   that are naturally entities (a projectile, a spawned hitbox) are entities,
   the `ActiveEffect` precedent.

Ruling on the transient-marker discomfort the spec asked to be judged
honestly: **the discomfort is correct about one pattern and incorrect about
another, and the tree already separates them.** Per-frame add/remove flag
components are banned (abilitykit.md D-F) and nothing in this plan does it:
one-tick signals ride tag *values* in a persistent container (jump request),
payload rides *slots* on persistent components (`PendingJumpSpeed`), duration
rides effect *entities*. What remains is marker *swap at transition
boundaries* with a single arbiter writer, which is not churn but the ECS's
native dispatch index, and it is what makes "a new mode is a new marker plus
a new system, editing nothing" true. Adopting one uniform representation to
soothe the discomfort would make something worse: rows for everything makes
movement systems scan ability bookkeeping; markers for everything spends
types per boolean and cannot stack; entities for everything adds a lookup
hop to per-frame movement math. The four-home rule is the honest answer.

**Alternative considered.** A dedicated activation entity per live ability
(abilitykit.md's deferred Stage 7 sketch). Rejected for the common case:
cross-entity joins per tick for release routing, lifetime management for
bookkeeping that fits in 24 bytes per row, and no batching win at tens of
rows. Effects already get entity treatment where batching is real.

### D-O. Held and mode abilities: the movement boundary (spec 6.4)

**Decision.** The boundary rule: **the ability layer owns the input lifecycle
and the legality gate; the movement layer owns simulation and transitions.**
Concretely:

- Charge and automatic fire are pure input lifecycle: rows plus ops, no
  movement involvement.
- Glide, cling, and mist are locomotion modes. The ability's `WhileActive`
  effect grants a *wish tag* (`movement.glide.wish`); a game transition system
  requests the mode while the wish and the mode's own physical conditions
  hold (airborne, surface contact, inside volume); the arbiter swaps markers
  by priority; the mode's locomotion system owns the physics and input
  reinterpretation; the projected mode tag (`movement.misted`) is what other
  systems and gates observe. Release or toggle-off ends the wish effect and
  the mode falls out on the next arbitration.
- Auto-revert (mist ends when leaving the mist volume) is movement-side: the
  transition system, which already polls the volume condition
  (`PhysicsQueries::OverlapShape`; there are no trigger enter/exit events in
  the engine), ends the wish effect through a small new helper
  (`EndEffectsGrantingTag(world, target, tag)`). The row poll (D-K pass 1)
  sees the dead effect entity and closes the row with reason `External`, no
  ability-side special case, no callback. The same helper serves the landing
  system clearing `movement.air_jump.used`.

Are held modes "abilities at all"? Yes for exactly two things: legality
(gates, costs, mutual exclusion authored as data) and input lifecycle
(press/release/toggle). Everything after entry belongs to movement, and the
wish-tag seam means movement never learns ability ids and abilities never
learn marker types.

**Rationale.** This is the landed division extended by one concept (the wish
tag). It keeps the arbiter as the single marker writer (a hard invariant in
`LocomotionMode.h`), keeps mode addition additive (proven by the game-side
`Climbing` mode in `test/framework/MovementTests.cpp`), and gives cling and
mist their auto-revert without ability-side world knowledge.

**Alternative considered.** An ability op that requests the mode directly
(`RequestLocomotionMode` as an op). Rejected: it would bypass the mode's own
physical eligibility (glide with no air under you), duplicate the transition
condition in two places, and put marker `ComponentTypeId`s into ability data.
The wish tag keeps eligibility in one system.

### D-P. Conditions are tag queries; markers are for dispatch (spec 3)

**Decision.** Activation conditions remain `GameplayTagQuery` over
`GameplayTagContainer`, unchanged. Component presence is not added as a
condition vocabulary. Rationale is O4's three points (stacking, budget,
hierarchy already exploited); the operational rule is D-N's "tags for
queryable state values, markers for which system runs." Mode markers still
influence gating, but through their projected tags, which is why the jump
gate reads `movement.grounded` and not `With<OnGround>`.

### D-Q. Authoring surface and extensibility (spec 6.6, editor requirement)

**Decision.** Three layers, shipped in this order:

1. **Text plus hot reload plus console.** `.sability`/`.seffect` are authored
   JSON with strict validation; the landed watcher/reloader picks up saves and
   re-installs definitions into live worlds (D-M). New console surface:
   `ability.list`, `ability.grant <entity> <name>`, `ability.activate`,
   `ability.inspect <entity>` (grants, live rows with held time, cooldown
   tags, active effects), plus an `ability.debug_log` cvar. This is the
   minimum inspectability bar for spec criterion 3 and ships with the runtime
   work.
2. **Kyusu references, never authors.** The `GrantedAbilities` component with
   asset-tagged schema fields makes the existing inspector picker work
   unmodified. Kyusu's only ability knowledge is "this component references
   ability assets."
3. **A dedicated ability editor, when asset count justifies it.** Follow the
   shudei template exactly (single-asset-type editor: browser panel, inspector
   over the definition, edit session with dirty tracking and undo commands,
   live preview via hot-reload re-install). The inspector renders gate,
   effects, and op buckets from schemas; op params render through
   `RuntimeSchema` like component fields. **Schema transport for
   game-registered ops:** the editor loads the game module DLL exactly as
   kyusu does (`GameModuleLoader`, ABI fingerprint, `SENCHA_GAME_MODULE`),
   runs its registration, and reads the op registry. Unknown ops in a loaded
   asset render as raw JSON with a warning, never silently dropped.

Extensibility walkthroughs (the spec's two cases):

- **(a) New ability of an existing shape, no code.** Author
  `abilities/rocket_ctx.sability` (Instant, gate `weapon.ctx.equipped`, cost
  ammo effect, on-activate op `game.ctx_fire` with rocket params) and any new
  `.seffect` files. Cook mints ids. Grant it via kyusu's picker on a pickup's
  `GrantedAbilities`, or `ability.grant` at the console. Zero engine or game
  recompiles if the ops it uses exist.
- **(b) New kind of behavior, game module only.** In game code: register an
  op (`ops.Register("game.hookline_fire", schema, fn)`), add the game
  components and the mechanism-named system that consumes the op's request
  (and, if it is a movement behavior, a marker, a locomotion policy, a
  transition edge, and one `RegisterLocomotionMode` call). All additive; the
  engine is not edited; the pattern is proven by the template game's wiring
  and the `Climbing` test mode.

### D-R. Slow time is a frame-loop capability, consumed by an ability

**Decision.** Fractional time dilation does not exist and is not built here.
When wanted, it is a `RuntimeFrameLoop` change (scale tick cadence while
keeping `FixedSimTime::DeltaSeconds` fixed, preserving determinism, with
presentation smoothing consequences owned there), surfaced through the
existing `time.timescale` cvar. The ability is then trivial: Toggle style,
`OnActivateOps` [`game.time_scale { scale: 0.3 }`], `OnEndOps`
[`game.time_scale { scale: 1.0 }`], with `OnEndOps` guaranteeing restoration
on any end reason. Recorded as a dependency so the ability plan does not
silently absorb frame-loop scope.

### D-S. Save and load (spec 6.5)

**Decision.** Safe-point saves only; in-flight ability execution is never
serialized. Argument, not assumption: both games are checkpoint/rest-point
genres; every transient in this design is either sub-second (dash, jump
request), re-derivable (mode markers re-arbitrate from tags and physical state
on the next tick), or deliberately droppable (an in-progress charge does not
survive a save in any comparable game, and mist reverting on load is correct
behavior at a save room). The durable set is exactly: grants (`AbilitySet` by
name, new serializer), attribute Base values, and designed-durable tags (both
via the existing, currently-unwired serializers). Active effects, rows, and
mode markers are runtime-only, and the save flow at a safe point ends live
rows (reason `Stripped` semantics) before snapshotting, which also drops
effect-granted tags coherently since the effect entities die with their rows.
Consequence accepted and documented: cooldowns reset on load. If a future
design demands mid-combat saves, the POD rows and per-mechanism state
components serialize naturally by name and tick offset; the statechart
alternative is the design under which this question would have been painful,
and it is the one not being built. The savegame *system* (slots, files,
versioning) is roadmap Track A scope, not this plan.

---

## 5. The recommended architecture, assembled

Runtime data model (engine, additions in bold):

- `AbilitySet` (grants, POD 16) ... landed
- `GameplayTagContainer` (state values, stacks, POD 32) ... landed
- `AttributeSet` (Base/Current, POD 16) ... landed
- `ActiveEffect` entities (duration state) ... landed
- **`ActiveAbilities`** (input-lifecycle rows, POD 8) ... D-K
- **`GrantedAbilities`** (authored asset refs, resolves to grants) ... D-M
- Component-type spend: 4 landed plus 2 new, of 256. Abilities, effects,
  statuses, and stats remain rows, ids, and assets: content never spends a
  type.

Definition model: `AbilityDefinition` gains style, `WhileActive`,
`InterruptedBy`, tick period, and four op buckets; `AbilityOpRegistry` maps op
names to native functions plus param schemas; definitions arrive from
`.sability`/`.seffect` assets or from code, keyed by name per World.

Per-fixed-tick flow (ordering is a documented invariant):

```
input capture (edges drained on first tick of frame)          [landed]
producers push AbilityActivation { actor, ability, edge }     [landed + edge]
grounding transitions, mode arbiter                            [landed]
ability activation system:
    row maintenance -> queue drain -> ticks                    [D-K]
    gate: tag query + cost against Base                        [landed]
    commit: cost, cooldown, WhileActive, op buckets            [landed + D-L]
verb consumers (jump execution, game translator systems)       [landed pattern]
attribute resolve, locomotion by mode marker                   [landed]
effect lifetime (expiry revokes tags, kills entities)          [landed]
physics (controllers consume slots)                            [landed]
```

Determinism: single-threaded FixedLogic systems over the logic span, FIFO
queue drain, array-ordered rows, chunk-ordered entities, all timing from
`TickIndex` and fixed dt, no wall clock, no randomness. Nothing here is
chunk-parallelized (counts are tens; the 1 ms profile gate is nowhere near).

Zone scope: all of this runs per active logic registry via
`ctx.ActiveRegistries`, so dormant zones pay nothing, the landed behavior.
Definitions install into each gameplay World by name (D-M).

---

## 6. Validation against the real ability inventories (spec 4)

Every row of the spec's tables, mapped. "Game" columns mean game-module code
following the landed patterns; none of it edits the engine.

| Ability | Style | Gate (tags) | Payload | Durational owner | New code beyond data |
|---|---|---|---|---|---|
| LF Jump | Instant | require `movement.grounded` (hier.), block cooldown | effect: 0.05 s `movement.jump.requested` | none | none (landed) |
| LF Double jump | Instant | require `movement.airborne`, block `movement.air_jump.used` | ops: apply jump-request effect; apply Infinite `movement.air_jump.used` effect | effect entity until landing clears it | landing-reset rule in grounding system (game) |
| LF Interact / Open map | Instant | context tags | op: `game.ui_open` / `game.interact` writes a request slot | none | game UI/interaction systems own the verbs |
| LF CTX: Normal / Rocket | Instant | require `weapon.ctx.equipped`, cost ammo attribute | op: `game.ctx_fire { mode }` writes weapon request slot | projectile entity | weapon fire system (game) |
| LF CTX: Charge | WhileHeld | as above; `InterruptedBy state.stunned` | WhileActive: `state.charging` (+ optional MoveSpeed multiplier); OnRelease op: `game.ctx_fire` scaled by `HeldSeconds` | row + effect | same weapon system reads charge from op context |
| LF CTX: Automatic | WhileHeld | as above | tick period 0.12 s; OnTick op `game.ctx_fire`; per-tick gate+cost re-check spends ammo | row | same weapon system |
| LF Dash | Instant | block `movement.dash.cooldown`, block `movement.dash.active` | cooldown effect; on-activate effect: 0.25 s `movement.dash.active` | Dashing mode: transition system requests while tag present; dash locomotion drives velocity; params component written at entry | marker + policy + transition edge (game) |
| LF Ground pound | Instant | require `movement.airborne` | 0.05 s request tag | pound mode or air-locomotion rule keyed on tag | small (game) |
| LF Slow time | Toggle | none | OnActivate op `game.time_scale 0.3`; OnEnd op restores 1.0 | frame loop | time-dilation mechanism (D-R dependency) |
| SINR Clawswipe / Melee | Instant | block `combat.melee.cooldown` | cooldown effect; 0.05 s `combat.melee.requested` tag | hitbox entity with lifetime, spawned by melee system | melee translator system (game) |
| SINR Jump / Interact / Map | | as LF equivalents | | | |
| SINR Glide | WhileHeld | require `movement.airborne` (hier.) | WhileActive grants `movement.glide.wish` | Gliding mode (marker + policy) while wish and falling | mode + transition (game) |
| SINR Cling | WhileHeld | require `movement.airborne`, require `surface.clingable.near` | WhileActive grants `movement.cling.wish` | Clinging mode reinterprets input on the surface plane; proximity tag granted by a game sensor system polling overlap | mode + transition + sensor (game) |
| SINR Turn to mist | Toggle | require `zone.mist_volume.inside` | WhileActive grants `movement.mist.wish` (+ modifiers) | Misted mode while wish and inside volume | mode + transition with volume poll (game) |

Five walkthroughs in full, then the stress test.

**Jump (regression proof: already true).** Space pressed: input system pushes
`{ pawn, movement.jump, Press }`. Activation: grounded tag present
(hierarchical), cooldown tag absent; applies cooldown effect (0.3 s tag) and
request effect (0.05 s tag). Same tick, `JumpExecutionSystem` consumes the
request tag into `PendingJumpSpeed` from `MovementProfile`; the mover applies
it; the arbiter flips to `InAir` next grounding pass, which retargets the gate.
Nothing in this plan changes a line of it.

**Dash.** Press: gate passes (no cooldown, not already dashing); cooldown
effect starts; `movement.dash.active` effect (0.25 s) granted. The game's dash
transition system sees the tag, captures direction into the dash params
component, and requests the `Dashing` marker at high priority; the arbiter
swaps `OnGround/InAir` out, projecting `movement.dashing`. Dash locomotion
drives velocity flat-out for the tag's lifetime; effect expiry drops the tag;
the transition stops requesting; grounding re-arbitrates to ground/air. Cancel
on hit: whatever applies the hit also ends the dash-active effect
(`EndEffectsGrantingTag`), and everything downstream follows. The ability
asset never knew any of this; a trap granting the same tag produces the same
dash, the cause/effect decoupling the spec's section 7 asks for.

**Charge (CTX: Charge).** Press: row opens, `state.charging` granted via the
WhileActive effect (with a MoveSpeed multiplier while charging if authored).
Hold: nothing accumulates; held time is derived. Release: row ends `Released`;
`OnReleaseOps` run `game.ctx_fire` with `HeldSeconds` in context; the op
clamps against `full_charge_seconds`, writes the weapon's pending-shot slot;
the weapon system fires the projectile entity this tick. Stunned mid-charge:
`InterruptedBy` matches, row ends `Interrupted`, WhileActive effect dies (tags
revoked), `OnReleaseOps` never run, no shot. Release arriving after the
interrupt is a no-op (no row).

**Cling.** Airborne near a clingable surface (proximity tag from the game's
sensor system): press opens the row, wish tag granted. The cling transition
requests `Clinging` while wish plus surface contact hold; the arbiter swaps
markers; cling locomotion reinterprets `MovementIntent.WishDir` on the
surface plane and the mode projects `movement.clinging` (animation and other
gates read it). Release: effect ends, wish drops, transition stops requesting,
arbiter returns to `InAir`. Surface crumbles away mid-cling: the transition's
own condition fails and the mode exits while the row stays open harmlessly
(wish persists; re-attach on regained contact is a game choice; a game that
wants the row closed ends the wish effect from the transition instead).

**Mist.** Press inside a mist volume: gate requires `zone.mist_volume.inside`
(granted by the game's volume-sense system polling `OverlapShape`; the engine
has no trigger events, so the poll is explicit and cheap). Row opens
(Toggle), wish granted, Misted mode enters, movement semantics change, mode
tag projects. Press again: row ends `ToggledOff`, wish effect dies, mode
exits. Walk out of the volume instead: the volume-sense system revokes
`inside`; the mist transition system ends the wish effect
(`EndEffectsGrantingTag`); next activation tick, row maintenance finds the
effect entity dead and closes the row with reason `External`. Auto-revert
falls out of liveness, with no ability-side special case and no callback.

**Hookshot (stress test, not shipping content).** Decomposition: aim is a
camera/game state; fire is `WhileHeld` ability whose `OnActivateOps` run
`game.hookline_fire` (game op spawning the hook projectile entity, storing the
owner); flight and impact are the projectile's own system over
`PhysicsQueries`; the branch on hit is that system inspecting the hit entity's
tags (anchor: grant `movement.hookpull.wish` on the owner; pullable: apply a
pull effect or velocity request to the target; enemy: apply a damage effect);
the pull is a `HookPull` locomotion mode entered by the wish, exactly the part
the spec already concluded was an ordinary system; retract and cleanup are
that mode's exit conditions (arrival, line break). Cancel at any point:
release ends the row (`Released`), `OnEndOps` run `game.hookline_release`
(despawn the hook, end the wish); stun ends it identically through
`InterruptedBy` plus `OnEndOps`. Every step is a mechanism this plan already
has; the "sequence" lives in world state (projectile lifecycle, mode
transitions), which is inspectable in the same debugger and console as
everything else. Honest cost statement: this is more game code than a
statechart asset would be, and that is the accepted trade; paying interpreter
complexity in the engine for an ability neither game contains would fail the
spec's own right-sizing criterion, and the repo's seam-deferral doctrine says
to wait for the second consumer. The tripwire is in section 8.

---

## 6.8 Roadmap reconciliation (proposed edits, owner decides)

- Track A "AbilityKit world sinks": reword. The sink interfaces named there
  (`IImpulseSink`, `IMontageSink`, `IHitQuery`, `ICueSink`) do not exist in
  code; this plan's op registry plus mechanism-named consumer systems is the
  replacement shape. The montage/cue halves stay blocked on the animation
  runtime as before.
- Track A "Input action mapping": this plan defines the contract it must emit:
  named actions bound to ability names, producing `AbilityActivation` press
  and release edges (and buffering windows, when built, live in that layer,
  not in AbilityKit).
- Track A "Scripting runtime": flag the contradiction with the ability spec's
  "no VM" constraint for an explicit owner call (O2). This plan is unaffected
  either way.
- This document takes over as the execution spec for the ability items of
  Track A; abilitykit.md remains the record for D-A through D-J and the landed
  spine.

---

## 7. What this plan deliberately does not build

Each with the trigger that would reopen it.

1. **A statechart / flow runtime (spec option C).** No shipping ability needs
   sequenced awaits; every candidate decomposes into lifecycle buckets,
   effects, tags, and modes. Reopen if a shipping game accumulates three or
   more abilities whose decomposition demonstrably sprawls (multiple
   single-use wish tags and translator systems per ability, authored order
   dependencies between them). If reopened, build it as a data upgrade of the
   same `.sability` (states referencing the same gate and op vocabulary), so
   today's assets and ops carry forward.
2. **An event bus / wake index for abilities.** Row maintenance polls tens of
   rows; effect-entity liveness is the reverse channel. Reopen only if
   profiling shows the poll on real content mattering (it will not at these
   counts) or if three or more systems independently reinvent ad hoc queues
   (the unwired `EventBuffer<T>` primitive is the starting point then).
3. **Activation entities.** Rows cover input lifecycle; effects and
   projectiles are already entities where batching is real (D-N).
4. **Per-ability component types or per-ability systems.** The budget spend
   is per mechanism, bounded by game mechanics, not content.
5. **The four fixed sink interfaces** from the earlier plan. Ops plus
   consumer systems replace them (D-L, 6.8).
6. **Mid-ability serialization.** D-S; revisit only with a real mid-combat
   save requirement.
7. **A targeting/lock-on system, combo graphs, and input buffering.** No
   section-4 consumer; buffering belongs to the input action mapping layer;
   melee needs a hitbox spawn, not targeting.
8. **The ability editor executable, first.** Sequenced third behind text plus
   hot reload and the kyusu picker (D-Q): the asset format and schemas are the
   architecture; the editor is a consumer, and shudei is the template when
   asset count justifies it.
9. **Time dilation.** A frame-loop capability this plan only consumes (D-R).

---

## 8. Rollout

Each stage is independently green and useful; tests follow
`test/framework/` fixture conventions (headless Worlds, in-test authoring).

- **Stage 0, hygiene.** Wire `RegisterGameplayTagSerializer` and
  `RegisterAttributeSerializer` (currently defined, never called); add
  registry `Update(name, def)` semantics; add a reap rule and test for
  `ActiveEffect` entities whose target died. Gate: tags and attributes
  round-trip a scene; a re-registered definition updates in place.
- **Stage 1, activation lifecycle (D-K).** Edges, styles, rows,
  `InterruptedBy`, end reasons, `EndEffectsGrantingTag`. Tests:
  press/hold/release/toggle matrix, held-time derivation, autofire cadence
  with per-tick cost, interrupt-versus-release payload rules, strip-mid-hold,
  external end via effect death, row-capacity overflow, determinism (two
  worlds, identical intent script, identical state). Gate: charge, autofire,
  and toggle fixtures pass headless.
- **Stage 2, ops (D-L).** Registry, op context, `sencha.apply_effect`,
  schema-described params. Tests: registration and duplicate rejection,
  unknown-op load failure, a test-registered op receiving `HeldSeconds` and
  writing a request slot. Gate: a data-authored ability fires a game op with
  authored params and runtime-derived scaling.
- **Stage 3, assets (D-M, D-Q layers 1 and 2).** `.seffect`/`.sability`
  formats and loaders, definition caches, install-into-world, hot-reload
  re-install, `GrantedAbilities` plus resolve, `AbilitySet` by-name
  serializer, console tooling. Gate: the code-registered jump is reproduced
  byte-for-byte in behavior from a `.sability`; editing the file on disk
  changes live behavior; kyusu grants an ability through the existing picker;
  grants survive a scene round trip.
- **Stage 4, reference content.** Dash, glide, cling, and mist built in the
  template game as the worked mode examples (markers, policies, transitions,
  sensor and volume polls), mirroring the section-6 walkthroughs, with
  `MovementTests`-style coverage. Gate: the five walkthrough abilities run in
  the template.
- **Stage 5, ability editor (deferred, D-Q layer 3).** Shudei-pattern app
  when asset count warrants; game-module loading for op schemas.

Open items left explicitly to the owner: the O2 scripting-roadmap
contradiction; ability-editor timing (Stage 5 trigger); the buffering window
default when input action mapping lands; whether charge curves stay scalar
params (this plan's default) or become a shared curve asset later.
