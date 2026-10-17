# AbilityKit — Sencha's Gameplay Framework

Status: **implemented** (AbilityKit 2, 2026-10). The design record and its
deviations are `docs/plans/abilitykit-2.md`; the Workbench that authors it is
`docs/editor/ability-workbench.md`.

AbilityKit is Sencha's gameplay framework, built the data-driven ECS way:
gameplay is **authored data** (tags, attributes, effects, abilities) interpreted by
**uniform systems** over **POD components**. It keeps the concepts of Unreal's GAS
-- attributes, effects, abilities, tags, cues -- without its object hierarchy.
There is no per-actor ability object; an entity's capability is its components and
the data they name.

> AbilityKit makes ordinary gameplay authoring easy without making unusual
> mechanics difficult or impossible. It decides *when and under what conditions*
> an operation runs; the subsystem that owns the operation decides what it does.

An ability is phases with ops and transitions, run as a per-actor record. What it
does to the world goes through the authored API -- verbs, leases and queries any
subsystem or game module declares (`docs/gameplay/authored-api.md`) -- so AbilityKit
names no gun, dash, beam or time effect. Loss Function's six abilities (below) are
the minimum vocabulary, not its limit.

## Rules

### D-A — Tag registry is a resource; per-entity tags are a POD component

`GameplayTagRegistry` is a per-`World` resource. Per-entity tags are the POD
`GameplayTagContainer`: up to 32 id-sorted tags with 16-bit stack counts, where a
revoke decrements and the last stack removes.

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

`AttributeSet` holds up to 16 attributes, each an id, a Base (persistent; instant
effects modify it) and a Current (Base with active modifiers, recomputed each tick).
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

### D-E — An ability is phases, run as a record; no state machine object

An ability's phases, ops and transitions are data ("Ability definitions"); one pass
runs every actor's records ("Runtime"). Tags still carry exclusion: an ability's
`owned_tags` and `cancel_tags` end conflicting activations, and admission is
conditions on tags and attributes. Costs and cooldowns are effects.

### D-F — Events are data a system pulls, never callbacks

Four forms by lifetime: derivable **conditions** (queried, e.g.
`Changed<Health>` + compare — stored nowhere); durable **facts** (a `Dead`
tag/component); transient **notifications** (a double-buffered `Events<T>` queue
resource); per-occurrence **payloads** (event entities, spawn→process→despawn).
"Health ≤ 0 → die" is a condition that produces a durable `Dead` tag plus an
optional `DeathEvent` in a queue. Never add/remove a transient flag component per
frame (archetype churn); clear event data in bulk instead.

### D-G — The world is reached through the authored API

Ops invoke verbs, hold leases and evaluate queries that their owning subsystems
declare: physics answers `physics.trace`, movement holds `movement.burst`,
animation takes `anim.hold`. A game module adds its own through the same
catalogs, and they appear in the Workbench's palette with no editor code.

### D-H — Serialize tags and attributes by name, definitions by asset path

`GameplayTagContainer` and `AttributeSet` serialize their entries by **registered
name**, resolved to ids through the registry on load (ids are registration-order
dependent, names are stable) — the same pattern as asset handles serializing as
paths. Effect/ability *definitions* serialize as asset-path handles. Active
effects and resolved `Current` values are runtime-only (omitted from schema);
persistence across save/streaming is the overlay's job, deferred. On the wire the
same holds: tags, attributes and abilities cross as their names' wire keys
("Replication").

### D-I — Input reaches abilities as ordered edges

Abilities read the tick's ordered press, release and interrupted-release edges
through the action each grant binds ("Grants and input"), the same on the
authority and in a client's predicted pass; requests reach the same admission by
name.

## Vocabulary

`Attribute`, `AttributeSet`
: A named float resource and an actor's flat block of them (Base, Current).

`Effect`, `ActiveEffect`
: A data-defined modification -- instant to Base, or a duration/infinite entity
  whose modifiers fold into Current -- with granted tags and granted abilities.

`Ability`
: An `ability.definition`: bindings, conditions, locals, costs, cooldown, phases.

`AbilityGrants`, grant entry
: What an actor may run, each grant naming an interned entry (key, ability,
  binding override).

`ActiveAbilities`, record
: An actor's running and lingering activations, each named on every machine by
  its activation identity.

Op, lease, query
: What a phase does: invoke a verb, hold a lease while the phase lasts, evaluate a
  query into a local; plus the kit's own `set`, `commit` and `cue`.

Cue, cue presentation
: A named occurrence an ability makes, and the cosmetic verbs and leases a
  `cue.presentation` asset says present it.

`GameplayTagContainer`
: A POD per-entity tag set with stack counts.

## Frame order

All in fixed logic, ordered with `After<>`:

1. `ActorTimeSystem` -- each actor's clock from its time leases.
2. `AbilitySystem` -- the pass ("Runtime"); on a client, its predicted pass.
3. Movement's composition, then `AttributeResolveSystem` and `EffectLifetimeSystem`.
4. `AbilityPresentationSystem`, where something presents.

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
   (`unreachable`, a warning), `no_exit`, `zero_time_cycle` over immediate and
   release transitions (a release stays true once it has happened, so a loop
   through one repeats within the tick), cost and commit placement (`commit_repeat`, `commit_twice`,
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
  priority order. A context naming a tag this World has not registered binds
  nothing until the tag exists: an unreadable context refuses, never admits. Two exclusive bindings at the top priority start nothing
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
press the same way. `AbilityActivationIdentity` folds the key, with the ability by
its wire key, and the grant it ran through into the one number every machine names
the activation by: what its leases' owner keys, its presentation and its cue
entries are keyed by. Two grants of one ability observing one press are two
activations; grant ids reach the owner verbatim, so a guess names its activation
as the authority does. The record's
`Sequence` is only this machine's start order.

**Admission** checks the definition, the activation conditions, `instances`, the
four live records, and that the start costs and an `on_start` cooldown can be
paid. A record takes a free slot or evicts the oldest lingering one; with neither,
the start is refused `Capacity`. **End** is the only exit and is idempotent: holds are released newest first,
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

## Animation

Abilities ask animation for things through the same door every producer uses:
`anim.hold` (a Held request for as long as the phase lasts) and `anim.request`
reach `RequestAnimation`, which the authority issues and a predicting client
guesses (`docs/gameplay/animation.md`, "Requests"). Reactions only a viewer needs
go through `anim.present` from a cue presentation, onto rig layers declared
`presentation` ("Presentation layers"). Animation plays on the actor's clock: a
dilated actor's content advances at its scale and re-anchors when the scale
changes.

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
  vector). An op run over a target list makes one entry per target, and an entry
  carries its ordinal among the op's entries at that target that tick, counted by
  the pass rather than read back from the ring.
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
`AbilityCueLogWire.h`). A record sends its activation key, state, phase and its
clock, end reason and element, pulse count and last hit, and the receiver computes
its identity from the key and the grant's id; the grant itself, leases, owned tags
and the trigger stay with the authority. Ids cross by name: an
ability by `AbilityRegistry::WireKey` (its path's `NameWireKey`), a tag by
`WireTag`, an entity as a wire entity. Each record is split into runs by how often
they change, so a pulse resends only its count and hit, and the hit is sent at
`kAbilityHitPointBits` and `kAbilityHitNormalBits` (about three centimetres and a
degree). A receiving machine gets the cue log and presentation state with the
records (`ActiveAbilities` derives both), and presents them with the same
`AbilityPresentationSystem` and the same first-sight rule.

The actor's own state goes to its owner, who predicts with it: `AbilityGrants`
(each grant's ability by wire key and its entry by the `NameWireKey` of the entry's
key, found in the owner's own interned entries; a grant whose entry the owner has
not interned is held but bound to no input), `AttributeSet` (attributes by name)
and `GameplayTagContainer`. Tags declared `replicate_to_all` in a
`gameplay.tag_declarations` asset (or marked by `SetReplicatedToAll`) go to every
machine instead, without their stack counts: what other players' presentation
reads, such as `Weapon.Firing`. The shared half holds as many tags as a
container can (`kGameplayTagSharedCapacity`), so none is ever dropped, in runs
of `kGameplayTagSharedRunLength`; a run still empty is not sent, so an actor
showing eight tags or fewer costs what one run does.
`ActorTimeLeases` go to everyone, so a remote machine animates a dilated actor at
its scale; each machine runs the actor clock from them.

The loss bounds, each pinned by `test/runtime/AbilityReplicationTests.cpp`:

- a record that began and ended between publishes still presents, through its
  linger (`LingerTicks`, four publishes at the defaults);
- a record's last pulses are lost only if every publish of its linger is;
- a discrete cue is lost only if eight newer ones are logged before a publish
  that carries it arrives;
- an older state applied after a newer one presents nothing again, and a log's
  serial wraps without losing order.

One firing actor costs a peer 340 B/s at the default cadence with its aim
sweeping, every pulse landing somewhere new (`CueBandwidthPerFiringActorIsMeasured`
fails above 360).

## Prediction

A client runs the ability pass for the pawn it predicts, on that pawn's command
timeline, as a guess (`AbilityPass` predicting): only the records it started
(`AbilityRecord::Predicted`), only the operations its ability's `prediction` level
allows — none at `none`; `Presentation` operations, cues and present entries at
`presentation`; `Simulation` operations too at `simulation`; `AuthorityOnly`
operations never — and a commit that validates against the replicated attributes
but pays nothing, starts no cooldown and changes no owned tag. Requests and
effect-held grants stay with the authority.

The guesses sit beside the authority's records, never over them. `ActiveAbilities`
holds the authority's six records, which the codec sends and a snapshot rewrites,
then `kAbilityGuessCapacity` slots for guesses, one per activation the authority can
hold live, so a client can guess every start the authority admits.
`AbilityPredictionJournal` (a `PredictionJournal`) takes each guess as it starts
and puts the guesses back after each snapshot. A guess the journal has no room
for is not made (`PredictionFull`): with sixteen undecided, the client waits for
the authority's word, and its input still reaches the authority as always. A client's pass changes only its guesses (`AbilityPass::Runs`): input,
`cancel_tags` and requests leave the authority's records as they arrived. A guess the authority
also started keeps running here, with the authority's extra pulses added; one it
ended ends here with the authority's reason; one it never started, once it has run
that command, ends `AuthorityDenied`, letting go of what it held. An authority
record beside a guess for the same activation is shadowed: not counted for
admission, not run and not presented (`IsAbilityRecordShadowed`), and an ended
guess stays until the authority's copy stops running.

The owner presents its guesses at once: discrete cues go to its machine-local
`AbilityPredictedCues`, never the replicated log, and the presenter remembers each
occurrence (`AbilityCueOccurrence`: activation, op, tick, target and ordinal) so
the authority's entry for it is not presented again, and an entry the guess did not
make, such as a target only the authority struck, still is. Records are followed by identity, not slot, and one
that went away and comes back on the wire is not presented again.

## Authoring

Abilities, effects and cue presentations are authored in Kyusu's Abilities
workspace (`docs/editor/ability-workbench.md`): the phase strip, the operations
palette built from the declared catalogs, checks that open the field at fault,
recipes (Blank, Instant, Held / Periodic, Charge / Release, Timed Movement,
Sustained Toggle), and a preview that runs the working data on a fixture character
with the production schedule.

## Worked example: Loss Function

Loss Function (`../loss-function-sencha-abilitykit`, built from the arena template)
authors its six abilities as content alone; its module names none of them.

| Ability | Shape | Mechanisms |
|---|---|---|
| CTX Primary | press: commit, trace, guarded damage, shot and impact cues, recover | costs, `physics.trace`, recipient guard, discrete cues |
| CTX Automatic | press-release: a pulse every six ticks paying a round per pulse | `while` pulses, `per_pulse` costs, pulse cues |
| CTX Charge | the normative charge: actor-time charge, release fires scaled damage | phase progress into a local, guarded transition, cooldown on commit |
| Vector Lunge | a timed `movement.burst` along the aim, completing into the end | `on: complete`, cooldown on start, `simulation` prediction |
| Vector Tether | trace, then a `movement.tether` to the struck point while held | hit guards, `point` anchor, presented beam |
| Overclock | a toggle holding `time.scale` and an `effect.hold` drain while Energy lasts | actor time, continuation on an attribute, cooldown on end |

The CTX modes are three more grants: keys 2 and 3 toggle abilities owning
`Weapon.CTX.Mode.Automatic` or `...Charge` and cancelling each other through
`cancel_tags`, key 1 cancels both; the fire abilities pick by binding `context`.
`Weapon.Firing`, the mode tags and `State.Overclocked` are declared
`replicate_to_all` so other players see them.

## Costs

Measured by `scripts/bench_abilities.sh` (profile preset, performance cores):

Per actor with grants, 9,016 bytes: `AbilityLocals` 4,560 (eight typed slots and an
entity list for each of ten slots), `ActiveAbilities` 2,256, `AbilityPresentationState`
624, `AbilityCueLog` and `AbilityPredictedCues` 584 each, `AbilityGrants` 392,
`ActorTime` 16. The four guess slots are 2,912 of those bytes, empty everywhere but
on the pawn a client predicts (`docs/deferred.md`).

| Actors | Pass, idle | Pass, every actor firing an automatic | Presenting them |
|---|---|---|---|
| 16 | 0.005 ms | 0.009 ms | 0.001 ms |
| 64 | 0.019 ms | 0.032 ms | 0.005 ms |
| 256 | 0.073 ms | 0.126 ms | 0.018 ms |

Medians per tick over 240 ticks after 60 of warm-up; "firing" is a pulse with a cue
every second tick on every actor. The pass is linear in actors, well under the 1 ms
mark at which a query is worth parallelising, so it stays serial. A firing actor
costs a peer 340 B/s ("Replication").

## Architectural smells

Be suspicious when new code:

- adds a per-entity ability object or a state machine class for an ability
- puts an ability, weapon or genre name in engine code (Overclock is content)
- calls physics, animation, audio or a game system from the kit instead of an op
  through the authored API
- registers an attribute-changed callback, or adds and removes a transient flag
  component every tick
- stores a `std::vector` inside a kit component instead of a fixed record
- serializes or sends raw tag, attribute or ability ids instead of names or wire keys
- presents from simulation code, or runs a gameplay operation from a presentation
- writes a client's guess over the authority's record instead of beside it
