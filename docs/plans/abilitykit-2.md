# AbilityKit 2: phased abilities, input binding, and the Ability Workbench

Status: **Approved (revision 3, 2026-10-13). Implementation in progress; see §17.**
Branch `claude/abilitykit-2` (worktree `../Sencha-abilitykit`) on `origin/main` at
fc94909a.

Inputs: *Sencha AbilityKit 2 — Gameplay Action & Visual Authoring Contract*
(2026-10-13, "the spec"), *AbilityKit 2 — Stage 0 Architecture Review* ("review 1")
and *Stage 0 Revision 2 Review* ("review 2"). Review 2 approved the architecture in
principle and asked for focused amendments; §0.2 dispositions them. Sections that
review 2 did not touch are unchanged from revision 2.

The governing sentence, from review 1: **AbilityKit makes ordinary gameplay
authoring easy without making unusual mechanics difficult or impossible.** The six
Loss Function abilities are the minimum vocabulary, not its limit. AbilityKit
decides *when and under what conditions* an operation runs; the subsystem that owns
the operation decides what it does.

---

## 0. Dispositions

### 0.1 Review 1 (resolved in revision 2)

| Review 1 § | Finding | Where |
|---|---|---|
| 1.1, 1.2 | Activation-local values; conditional execution after a query | §3.3, §3.4 |
| 1 (acceptance) | CTX Charge passes the real compiler | §16 |
| 2.1–2.5 | Grant-owned bindings, stable grant ids, safe consumption, interruption, rapid transitions | §2 |
| 3 | Activation, continuation and cancellation are separate | §3.5 |
| 4 | Atomic commitment, cost kinds, cooldown timing | §5 |
| 5 | Sustained vs discrete presentation; activation identity | §6 |
| 6 | Registered conditions, values and held operations | §3.6, §4 |
| 7 | Stable keys, workflows, diagnostics, honest preview | §8.1, §9, §10 |
| 8 | Authorable presentation | §6.6 |
| 9 | Overclock is time dilation | §7 |
| 10–13 | Cleanup proof, staging, ledger, acceptance | §11–§15 |

### 0.2 Review 2 (this revision)

| Review 2 § | Finding | Amendment | Where |
|---|---|---|---|
| 1 | Counts + one `Interrupted` flag lose order; same-tick presses vs `instances: 1` | Tick records carry an **ordered list of action edges** (`Press`, `Release`, `InterruptedRelease`), which also preserves order across actions, end to end through capture, attribution, tick, wire, collapse and server feed. The ability pass walks that list and **runs each started activation to quiescence before the next edge**, so three presses of an instant ability are three complete activations | §2.6, §3.7 |
| 2 | Replay must not use stale local time scales | Actor-time leases carry `FromTick` / `UntilTick` and are replicated as history; replay computes each replayed tick's scale from them, so a server-only dilation is replayed at its true effective tick. Scales above 1 substep the character step. AI cadence contract stated | §7.2 |
| 3 | The ring's guarantees were overstated | Three presentation channels with stated loss conditions, latency and bandwidth: sustained (state), **pulse counter + last hit on the replicated record, with an end linger** (high-cadence fire), and the ring for other discrete occurrences. Compared with Sencha's message channels. No "every shot presents" claim | §6.3–§6.5 |
| 4 | `anim.request` cannot run on presenting machines | A cosmetic `anim.present` verb writes a machine-local, never-replicated presentation request set read only by rig layers declared `presentation`, which cannot carry root motion or Gameplay-scope events | §6.7 |
| 5 | Prediction must not be binary | Per-operation prediction category (`AuthorityOnly`, `Presentation`, `Simulation`) declared by each verb and lease; an ability's `prediction` level (`none`, `presentation`, `simulation`) caps what a client executes. Shown for CTX Primary, CTX Charge and Overclock | §6.8 |
| 6 | `valid(Hit)` is not a valid recipient | `hit` locals separate "surface hit" from "recipient"; an `entity` condition checks liveness and required components; the compiler requires it wherever an argument declares a required component | §3.3, §3.4, §16 |
| 7 | Ledger directions | Recorded | §14 |

---

## 1. Current state (what the plan builds on)

Revision 1's audit stands: there is no shipped ability behaviour to preserve;
definitions are C++-registered; `CanAfford` admits on missing data; effects have no
removal, tick in float seconds and strand tags; jump left the kit because
reconciliation cannot recreate spawned effect entities; the authored API is
synchronous and admission-only, with "producer ports", "verb classification" and
"prediction semantics" reserved for the consumer that needs them; only the local
pawn's movement is predicted.

**Input** (`InputFrame.h:69-163`; `InputActionResolve.h:62-117`;
`InputActionResolve.cpp:310-421, 578-640`; `NetPlayerCommand.cpp:11-174`)
- Capture keeps duplicate presses in six vectors, chronological within a vector
  but unordered across them; the latch then ORs them into bitsets, so
  press-release-press, two taps, and release-then-repress-while-held all collapse.
- Contexts settle once per frame, before the frame's ticks
  (`InputActionResolveSystem.cpp:108-152`).
- Gamepad buttons emit an edge only when the OR across pads changes; trigger
  thresholds are judged on the end-of-frame snapshot only.
- `InputActionValue` is 12 bytes with 3 bytes of padding. Nothing outside the
  resolver and the net codec reads its flags directly.
- The command wire writes 4 flag bits per action; backlog collapse ORs edges
  (`CarryEdges`), so several taps in shed ticks become one.

**Time** (`time/FrameClock.h:41-55`; `RuntimeFrameLoop.cpp:150-153`;
`CharacterTickStep.cpp:55-117`; `CharacterMoverPool.cpp:245-297`;
`AnimPlayback.cpp:3-8`; `EffectSystem.cpp:53-173`; `PawnStateReplay.cpp:97-103`)
- One global, `Replicated` `time.timescale` cvar scales how many ticks run per wall
  second; it never changes a tick's length. Nothing scales one actor's time.
- Movement kernels, character sweeps (Jolt `CharacterVirtual`), root motion and
  effect ticking already run **per entity** with one global dt.
- The rigid-body world is one `PhysicsSystem::Update(dt)`; no per-body time.
- Animation computes clip time from `(tick - StartTick) * tickSeconds * Rate`, so a
  changing scale needs re-anchoring.
- Replay uses one dt for every replayed tick.
- Attribute modifiers fold in storage order, so several Multiply modifiers can
  differ by an ULP between machines.
- No AI decision system exists yet (roadmap Track A item 7).

**Presentation and replication** (`AudioService.h:25-118`;
`AnimEventSystem.h:58-82`; `AnimRequestJournal.h:43-48`; `VerbRegistry.h:38-54`;
`ReplicationChangeStore.h:17-149`; `NetChannel.h:12-44`; `CharacterFacts.h:55-95`)
- Audio is non-positional; there is no VFX, particle, decal, camera-shake or
  light-flash system (`docs/renderer/open-work.md:19`).
- `RequestAnimation` issues on the authority, predicts for the predicted pawn, and
  refuses everything else; there is no machine-local presentation request.
- Rig layers with no selector are request-keyed: the newest request claiming the
  layer names its behavior (`AnimRigData.h:28-42`).
- `VerbDefinition` has no gameplay/cosmetic classification. Entity arguments carry
  a required component as authoring metadata (`DataSchema.h:62-63`,
  `DescribeAuthoredComponentTarget`).
- Snapshots (30 Hz by default) send the current value of every field run changed
  since the peer's acked floor; values that change and change back between publishes
  are never seen; unacked changes are resent each publish.
- Message channels: `UnreliableSequenced` (newest wins, stale packets dropped,
  nothing resent) and `ReliableOrdered` (resend, ordered, 32-packet window).
- Impulse animation requests are live for one tick and very likely never present on
  remote clients.
- Remote pawns receive no velocity, support state or tags (D18).

**Identity in data documents** (`DataPath.h:11-13`; `DataProperties.cpp:511-552`;
`EditorScene.h:154-158, 265`)
- Generic data rows are keyed by positional JSON path; generic add mints no id and
  generic duplicate copies one. The scene's random 64-bit persistent id is the only
  random stable-id precedent.

---

## 2. Architecture, ownership, grants and input

### 2.1 Components and owners

| Thing | Owner | Storage | Replication |
|---|---|---|---|
| What an actor may run | AbilityKit | `AbilityGrants` (per actor) | owner-only |
| What is running, plus recently ended | AbilityKit | `ActiveAbilities` (per actor, hook-free; live and lingering records, §6.4) | all relevant peers |
| Activation-local values | AbilityKit | `AbilityLocals` (per actor) | local only (a predicting client computes its own) |
| Discrete non-pulse occurrences | AbilityKit | `AbilityCueLog` (per actor ring) | all relevant peers |
| Effects, tags, attributes | effects / tags / attributes | existing components and effect entities | owner-only attributes; tags by declaration (§6.9) |
| Self-motion sessions | movement | `MotionBurst`, `TetherSession` | predicted |
| Actor time | time domain (§7) | `ActorTimeLeases`, `ActorTime` | all; predicted for own pawn |
| Authoritative animation requests | animation | `AnimRequestSet` (existing) | existing codec |
| Local presentation animation | animation | `AnimPresentationRequests` (new, §6.7) | never |
| Instant operations | each verb's provider | — | — |
| Held operations | each lease's provider | provider's own state | provider's decision |

Per-actor components rather than activation entities: they move with the actor,
replicate without prefab spawns, can join the pawn's predicted set, bound
concurrency, and need no structural change per activation. Capacities (4 live + 2
lingering records, 16 grants, 8 local slots, 8 ring entries, 8 time leases) are
measured starting points in one header, not product limits; overflow fails with a
typed diagnostic; changing them is an ABI change, so Stage 10 measures real content
before the first release that freezes them.

### 2.2 Grants and effective bindings

```cpp
using AbilityGrantId = StrongId<struct AbilityGrantIdTag, std::uint32_t>;   // per actor, never reused

struct AbilityGrantSource
{
    AbilityGrantKind Kind;        // Authored (prefab), Effect, Request (ability.grant verb)
    EntityId Effect;              // generational; valid for Kind == Effect
    std::uint64_t EntryKey;       // the authored grant entry's stable key
};

struct SENCHA_COMPONENT("sencha.ability_grants") AbilityGrants
{
    struct Entry { AbilityGrantId Id; AbilityId Ability; AbilityGrantSource Source; AbilityBindingIndex Binding; };
    Entry Entries[kAbilityGrantCapacity]; std::uint8_t Count = 0; AbilityGrantId NextId{ 1 };
};
```

- **One grant-entry schema** is used by a prefab's `AbilityGrants` scene form and
  by an effect definition's `granted_abilities`:
  `{ key, ability, binding?: { action, policy, priority, consume, when_blocked,
  fallback, context, on_interrupt } }`. Every field present overrides the ability's
  `input_default`; every field absent inherits it. The same ability asset is bound to
  `Input.PrimaryFire` on one pawn and `Input.AltFire` on another by two grant entries,
  never by a copy of the ability.
- Effective bindings are compiled per World into an `AbilityBindingTable`
  (resolved `InputActionId`, policy, priority, compiled tag query); the grant entry
  holds its index. The table rebinds on asset reload like every other binding cache.
- An activation stores the `AbilityGrantId` it started from. Revoking a grant ends
  exactly the activations that hold that id; another grant of the same ability, and
  its activations, are untouched. Removing or reordering entries never changes an id.
- Effect-sourced grants are added when the effect applies and removed by the
  effect's end through the same path that revokes its tags. An equipped weapon is an
  Infinite effect granting its abilities; there is no loadout asset kind.

### 2.3 Three input layers

1. **Device → action**: the existing resolver, emitting ordered action edges (§2.6).
2. **Action → ability**: `AbilityInputResolve`, a step of `AbilitySystem`, reading
   `InputActionSources::TickFor(actor)` for actors with grants.
3. **Request → admission → execution**: shared by players, bots, verbs and tests.

A bot is given its own `InputActionSource` and writes semantic action edges and
values, so it reaches its grants through the same resolver and binding table.
Scripts and verbs that name an ability use `ability.activate` / `ability.cancel`,
which skip binding and never skip admission.

### 2.4 Resolution, priority and consumption

At each `Press` edge of action A (§2.6), in edge order:

1. **Candidates**: grant entries whose effective binding names A and whose
   `context` matches the actor.
2. **Order**: priority, descending. Nothing else. No source ranking.
3. **Ambiguity**: if two or more `exclusive` candidates share the highest priority,
   nothing starts; the trace records `BindingAmbiguous` with every candidate; the
   Workbench and the runtime overlay show it.
4. **Exclusive**: the highest candidate runs admission. If admitted, it starts and
   consumes A. If refused, the default (`when_blocked: consume`) stops resolution
   there: the refusal is traced and the binding's `blocked` presentation plays (dry
   fire). Only a binding authored `fallback: true` lets a refusal continue to the next
   lower priority.
5. **Observe**: observe bindings at any priority start whenever admitted and never
   consume. The compiler warns when an observe binding's ability has costs.

Release edges are routed to the live activations the action started (`Trigger` on
the record), never re-resolved, so a mode switch mid-hold cannot misroute a release.
Static validation: within one source asset, equal-priority exclusive entries on one
action with contexts that are not provably disjoint are an error; across sources the
Workbench's **effective-binding view** composes a chosen fixture and reports the same
conflicts as warnings.

### 2.5 Interruption

An `InterruptedRelease` edge marks an action's fall that was not a genuine release:
context deactivation or suspension, a higher context claiming the control, profile
loss, and focus loss (synthetic device releases are tagged at capture). Rules:

- An interrupted fall never sets `Fired`, in any mode (D2). This reverses
  `DeactivatingAContextFiresAReleaseMode` and
  `LosingTheProfileFiresAReleaseModeExactlyOnce` and `input.md:276-282`.
- Each binding's `on_interrupt` is `cancel` (default) or `release` (treat as a
  genuine release, for channelled abilities that should complete). `cancel` ends the
  activation `InputInterrupted`.
- Because edges are ordered, "genuine press, then interrupted fall" in one tick
  starts the activation and then cancels it: an instant ability started by a genuine
  click completes (it ran to quiescence on the press, §3.7); a charge is cancelled
  without firing.
- Possession change and peer loss emit no input edge and need none: the record's
  `TriggerSource` no longer matches the actor's `InputActionSourceRef`, and the sweep
  ends it `InputLost`.
- Ambiguity resolves toward cancel: a genuine key-up in the same frame as a context
  loss is attributed after the context change (§2.6) and reads as interrupted.

### 2.6 Input edges: representation through capture, tick, wire and server

**Representation.** Each tick record carries an ordered list of action edges,
`InputActionEdge { std::uint8_t Action; InputEdgeKind Kind; }` with
`InputEdgeKind { Press, Release, InterruptedRelease }`, at most 32 per pass. A
rise nobody pressed for (a context returning over a held key) is silent, so a
`Release` may follow with no `Press` before it. The list preserves where each interruption falls and the order of edges
across actions (press Reload, then press Fire). The per-action flags
(`Held`, `Pressed`, `Released`, `Fired`, `Interrupted`) stay as summaries derived
from the list, for existing consumers; a Released-mode `Fired` requires a genuine
`Release`.

**Capture (per frame).** `InputFrame`'s six edge vectors become one ordered device
log, `InputTransition { InputControl Control; bool Down; bool Synthetic; }`, at most
64 per frame, in SDL event order. Held bitsets stay. `ReleaseAllHeld` and
`PlatformEventRouter::ReleasesMissedBefore` append `Synthetic` ups.
`ConsumeKeyPressed` (no production callers) is deleted.

**Attribution (per frame, per clock).** In `PreSimulate`, after contexts settle for
the frame, each clock turns the frame's device log into action edges and appends them
to that clock's pending list, in order:

1. first, an `InterruptedRelease` for every held action whose binding the frame's
   context change, claim change or profile change dropped;
2. then each device transition, mapped through the frame's active contexts and
   claims to the action it drives, producing a `Press` or `Release` when the action's
   held state changes (a synthetic device up yields `InterruptedRelease`).

Pending lists accumulate across zero-tick frames. This is the whole replacement for
the bitset latch: attribution happens with the context set that was active when the
transition arrived.

**Tick.** The first tick of a frame takes the clock's pending list into its record;
later ticks of a catch-up burst get an empty list and held state, as edges behave
today.

**Overflow.** A full pending list accepts no more edges; held state stays exact. When
a record is taken, any action whose walked edges end in a different held state than
its actual state gets one `InterruptedRelease` appended if it is actually up, and
nothing if it is actually down (it stays held with no start). Overflow therefore never
invents a start or a genuine release. It is counted in input diagnostics; 32 edges in
one frame window is beyond human input.

**Wire.** Per record: 6 bits of edge count, then per edge 6 bits of action index
(the existing `kActionCountBits`) and 2 bits of kind; per action the 5 summary flags.
The common case (one edge) adds 14 bits per record. `CarryEdges` concatenates the
older record's list before the newer one's, with the same overflow rule, so taps in
shed ticks survive collapse in order. Starvation repeats carry an empty list. The
authority's `PeerCommandFeedSystem` copies the list into the peer source's tick record
unchanged, so the server's ability pass walks the same edges the client walked.
`kNetProtocolVersion` 6 → 7; `RecordBits`, `WriteRecord` and `ReadRecord` change
together; `ButtonsCostNoAxisBits` is updated.

**Out of scope, recorded in `docs/deferred.md`:** a trigger threshold crossed and
uncrossed inside one frame (needs an axis event log; trigger: a measured lost trigger
tap).

---

## 3. Phases, activation-local values and conditions

### 3.1 Lifecycle

```
CanActivate (pure) ─► Start ─► phase ⇄ phase … ─► End(Completed) ─► linger (§6.4)
          │                └──── cancel trigger / continuation failure / grant loss / … ───► End(reason)
          └─ refused (typed reason, failing condition key)
```

`Start` claims a live slot, stamps keys and seed, pays `start` cost groups
atomically with admission (§5), applies owned tags, cancels the actor's activations
matched by `cancel_tags`, and enters the first phase. `End` is the only exit,
idempotent, releases every lease by owner key (§11), writes the trace, and turns the
record into a lingering record for presentation. Lingering records are invisible to
admission, `instances` and concurrency. Typed end reasons: `Completed,
InputReleased, InputInterrupted, InputLost, GrantRevoked, ContinuationFailed,
CancelTriggered, CancelledByAbility, TargetLost, CommitFailed, LeaseRefused,
ActorEnded, ZoneDetached, AuthorityDenied, HotReload, ExplicitCancel, StepBudget,
CapacityExceeded`.

### 3.2 Phases

```
phase := key, name
         timing:    { domain: actor | simulation, progress_ticks? }
         enter:     [op]                 once, on entry
         while:     { every: ticks, first: on_entry | after_interval, ops: [op] }   pulses
         hold:      [lease op]           acquired on entry, released on exit
         exit:      [op]                 once, on a transition out (not on End by cancel)
         present:   [{ cue }]            sustained presentation while in phase (§6)
         continue:  [condition]          phase-scoped continuation (§3.5)
         then:      [transition]         ordered, first match wins
transition := key, on?: release | timeout(ticks) | complete,  when?: condition,  to: phase key | end, reason?
```

A transition with no `on` is evaluated right after the phase's op lists in the same
step, which is how branching on a query result happens without waiting. Zero-time
cycles are a compile error; the runtime caps transitions per record per tick
(`ability.step_budget`, default 8) and ends a record that exceeds it `StepBudget`.

### 3.3 Activation-local values

An ability declares up to 8 slots of typed locals:

| Type | Slots | Empty value | Written by | Read by |
|---|---|---|---|---|
| `bool`, `int`, `float` | 1 | false / 0 | `set`, `query` | arguments, conditions |
| `vec3` | 1 | zero | `set`, `query` | arguments |
| `entity` | 1 | invalid | `set`, `query` | arguments, behind an `entity` guard (§3.4) |
| `hit` | 2 | no hit | `query` | fields `point`, `normal` (valid when `hit(l)`), `entity` (an entity source, possibly invalid even when `hit(l)`) |
| `entities` (≤ 8) | 2 | empty | `query` (e.g. overlap) | `targets:` of leases and invokes, each element behind an `entity` guard |

- `hit` means "the trace struck something": a world surface, a static body or an
  entity. It says nothing about whether `entity` is alive, has components, or is a
  valid recipient of anything.
- Lifetime is the activation: cleared at Start, discarded at End.
- Writers are `set` (from a port, a constant, an attribute value, or another local)
  and `query` (a registered query's result, type-checked against the local at bind).
- Entity values are generational `EntityId`s and are revalidated at every read.
- Stored in `AbilityLocals`; not replicated; a predicting client computes its own.
- In the Workbench, locals are a short named list with type pickers; every argument
  row offers "← local" beside "← port" and "constant".

Ports are read-only live values: `actor`, `instigator`, `aim.origin`,
`aim.direction`, `phase.progress`, `phase.ticks`, `pulse`, `time.scale`.

### 3.4 Conditional execution and recipient validation

- Every op may carry `when: condition`. A false guard skips the op and traces
  `Skipped(guard key)`.
- **`hit` guard.** An argument sourced from `point` or `normal` of a `hit` local must
  be guarded by `hit(l)` (`unguarded_maybe_empty`).
- **Recipient guard.** An Entity argument whose schema declares a required component
  (`Reference.ComponentIdentity`; for `effect.apply`'s `target`, `sencha.attribute_set`)
  and is sourced from a local must be guarded by an `entity` condition on the same
  source whose `has` includes that component (`unguarded_recipient`). An Entity
  argument with no declared component, sourced from a local, still needs an `entity`
  guard (liveness). `entity` implies a live, generation-matched entity.
- **Runtime rule.** The guard is evaluated at execution, and the provider checks
  again: `effect.apply` refuses a target that lacks an attribute set or any attribute
  the effect modifies (`Refused`, traced). A stale entity fails the op closed
  (`Skipped(stale)`).
- Op lists run in order within a step, so `query → guarded ops` is the branch right
  after a query. Larger branches use a `when` transition into another phase in the
  same step.

### 3.5 Activation, continuation, cancellation

Three authored lists on the ability, each a list of keyed conditions:

- **`activation`**: must hold to start. Binding `context` is an activation condition
  only.
- **`continuation`**: must keep holding while live, checked in the sweep each tick;
  the first failure ends the record `ContinuationFailed(key)`. A phase may add
  phase-scoped continuation (`continue`).
- **`cancel_on`**: triggers that end the record `CancelTriggered(key)`: a condition
  becoming true, an authored event (from a source filter), or a local becoming empty
  (`target_lost`, ends `TargetLost`).

Always-on triggers that are not authored: the record's grant revoked, its input source
lost, its actor ended, authority denial, hot reload. A failed activation condition
never cancels a running record unless the author repeats it under `continuation`.

### 3.6 Conditions

```
condition := key,
  tags:      tag query (all / any / none, hierarchical)
| attribute: { name, op: < <= > >= == !=, value | local }
| local:     { equals | compare }
| hit:       local key                    the trace struck something
| entity:    { of: entity source, has?: [component identity], tags?: tag query }   alive, generation-matched
| phase_ticks / pulse: compare
| input:     held | not_held
| query:     registered authored query with Bool result, arguments from constants / ports / locals
| all / any / not: of conditions (depth ≤ 3)
```

Tags, attributes, locals, entities and counters are evaluated inline. `query`
reaches any query a module registers through the existing `AuthoredQueryDispatcher`.
The combinators are bounded boolean structure, not an expression language.

### 3.7 The tick algorithm, and three presses in one tick

`AbilitySystem` runs once per fixed tick, in five global steps:

1. **Sweep** (all actors, chunk order): grant and source checks, continuation,
   `cancel_on`.
2. **Input** (actors in chunk order): walk the actor's source record's edge list in
   order. `Press A` → resolve (§2.4) → admit → Start → **run to quiescence**.
   `Release A` → each live record started by A observes the release → run to
   quiescence. `InterruptedRelease A` → `on_interrupt` decides cancel or release.
3. **Requests** (`AbilityRequestQueue`, FIFO): Start → run to quiescence.
4. **Advance** (all live records not entered this tick): advance phase time in the
   phase's domain, run due pulses, evaluate timeouts and condition transitions → run
   to quiescence.
5. **Linger**: expire lingering records past their window (§6.4).

**Run to quiescence** executes a record until it waits on a future tick or a release,
or ends: enter ops (and the first pulse when `first: on_entry`), then the `then`
transitions in order; a transition runs exit ops, releases and acquires leases, and
enters the next phase; repeat within the step budget. Input precedes advance because
a tick's edges happened in the frame window that precedes the tick's simulated
interval; a release and a cadence pulse in the same tick therefore end an automatic
weapon without a further pulse.

**Worked example.** `ctx.primary` is instant: one phase `Fire` whose enter ops commit
one ammo, trace, apply damage behind a recipient guard and emit `cue.muzzle`, and
whose only transition is `to: end`. `instances: 1`, no cooldown. A frame at a low tick
rate delivers, in one tick N, the edge list
`[Press Fire, Release Fire, Press Fire, Release Fire, Press Fire]`:

| Edge | What runs | State after |
|---|---|---|
| Press 1 | resolve → admit (no live record) → Start r1 (key {N, primary, 0}) → Fire enter ops → `to: end` → End(Completed) | r1 lingering; 0 live |
| Release 1 | no live record started by Fire | — |
| Press 2 | admit → Start r2 ({N, primary, 1}) → … → End | r2 lingering; 0 live |
| Release 2 | — | — |
| Press 3 | Start r3 ({N, primary, 2}) → … → End | r3 lingering; action still held |

Three activations, three commits, three muzzle occurrences, in edge order, all on
tick N; the server walks the same list from the command and produces the same keys.
A cooldown of D ≥ 1 would refuse presses 2 and 3 with `Cooldown`, which is the
author's rate limit. For `ctx.charge` (`press_release`) the same list yields: Press 1
starts Charge (waits); Release 1 takes `t_early` (0 < 6 ticks) to Recover (waits 20
ticks); Press 2 is refused (`instances: 1`, Recover is live) and consumed; Press 3
likewise. Tests: `ThreePressesOfAnInstantAbilityActivateThreeTimes`,
`PressesBeyondALiveInstanceAreRefusedAndConsumed`,
`ReleaseIsProcessedBeforeTheSameTicksPulse`.

---

## 4. Typed execution

### 4.1 Op kinds

| Op | Purpose | Mechanism |
|---|---|---|
| `invoke` | instant operation | a verb through `VerbDispatcher::Invoke`, provenance `{Producer = actor, Instigator, Parent, Tick}`; `targets:` repeats it over an `entities` local |
| `query` | observe into a local | an authored query through `AuthoredQueryDispatcher::Evaluate`; `Unavailable` leaves the local empty |
| `lease` (in `hold`) | held for the phase | a lease through `LeaseDispatcher` (§4.2), owner = activation |
| `set` | write a local | AbilityKit |
| `commit`, `cue` | pay cost groups; emit a discrete occurrence | AbilityKit (§5, §6) |

Physics traces and overlaps are registered queries: `physics.trace` (result
`{hit, entity, point, normal}`) and `physics.overlap` (entity list), with an `ignore`
entity argument (prerequisite: an ignore filter on `PhysicsQueries`).

### 4.2 Classification shared by verbs and leases

Verb and lease definitions gain two contract fields (D16):

- `AuthoredScope { Gameplay, Cosmetic }`. Cosmetic operations produce no simulation
  state. Producers declare their scope in `VerbInvocationSource` and the dispatchers
  refuse a mismatch (`ScopeMismatch`), so isolation is enforced at runtime as well as
  by the compiler: a presentation producer cannot invoke a gameplay verb, and gameplay
  never depends on a cosmetic one.
- `Prediction { AuthorityOnly, Presentation, Simulation }` (§6.8). `AuthorityOnly`
  never executes on a client; `Presentation` is cosmetic and runs wherever
  presentation runs, including a predicting client; `Simulation` is a predictable
  state change of the predicted pawn, journaled and covered by replay.

### 4.3 Leases: the held-operation catalog

Verbs cannot be held: they return admission, not a handle. A held operation is a
**lease**, declared in a fourth sibling catalog on the existing `AuthoredCatalog`
core, beside verbs, queries and events:

```cpp
struct AuthoredLeaseDefinition
{
    std::string Name, DisplayName, Description, Category;
    DataFieldSchema Arguments = AuthoredRecordRoot();
    AuthoredScope Scope;              // §4.2
    AuthoredPrediction Prediction;    // §4.2
};
```

A provider binds `Acquire(owner, arguments) → LeaseAdmission` and `Release(owner)`
through a token, exactly as a verb implementation binds, and must end anything whose
owner is gone when it next runs (§11). The `LeaseOwner` is
`{AbilityActivationRef, op key}`. Engine leases:

| Lease | Provider owns | Scope | Prediction |
|---|---|---|---|
| `effect.hold` | effects (`EffectOwner` + `RemoveEffectsOwnedBy`) | gameplay | AuthorityOnly |
| `anim.hold` | animation (Held `RequestAnimation`, existing journal) | gameplay | Simulation |
| `movement.burst` | movement (`MotionBurst`) | gameplay | Simulation |
| `movement.tether` | movement (`TetherSession`) | gameplay | Simulation |
| `movement.mode` | movement (`RequestLocomotionMode` for the scope) | gameplay | Simulation |
| `time.scale` | time domain (`ActorTimeLeases`, §7) | gameplay | Simulation (own pawn) |
| `audio.loop` | audio | cosmetic | Presentation |

A game module declares its own leases in `OnRegisterVocabulary` and binds them in
`OnStart`, like verbs. AbilityKit never learns what a lease does.

### 4.4 Verbs added

| Verb | Scope / prediction | Semantics |
|---|---|---|
| `effect.apply {target: Entity(sencha.attribute_set), effect, scale = 1}` | gameplay / AuthorityOnly | immediate `ApplyEffect` with scaled modifiers; `Refused` if the target lacks an attribute set or any modified attribute; unowned |
| `ability.activate {target, ability}` | gameplay / AuthorityOnly | admits a request into `AbilityRequestQueue` (bounded, `QueueFull`) |
| `ability.cancel {target, ability}` | gameplay / AuthorityOnly | ends that ability's live records `ExplicitCancel` |
| `ability.grant` / `ability.revoke {target, ability, key}` | gameplay / AuthorityOnly | `Request`-kind grant entries for scripts and tests |
| `audio.play {clip, gain, at?}` | cosmetic / Presentation | one-shot through `AudioService` (non-positional until audio gains a listener) |
| `anim.present {target, intent, lifetime: impulse \| fixed, ticks}` | cosmetic / Presentation | §6.7 |

`anim.request` and `anim.cancel` are reclassified gameplay / Simulation (their
existing behaviour). `spawn.prefab` stays deferred until the first ability needs a
projectile.

---

## 5. Atomic cost and cooldown commitment

### 5.1 Five kinds, authored distinctly

| Kind | Authoring | Paid |
|---|---|---|
| Activation cost | cost group `when: start` | at Start, atomically with admission |
| Commit cost | cost group `when: commit`, `repeat: once`, paid by `commit {groups}` | the first time a `commit` names it; later executions are a traced no-op (`AlreadyCommitted`), and the compiler warns if a second execution is reachable |
| Per-pulse cost | cost group `repeat: per_pulse`, `commit` inside `while` | once per pulse, keyed by pulse index; the compiler requires `per_pulse` for a commit inside `while` and refuses it elsewhere |
| Sustained consumption | an `effect.hold` lease of a periodic effect, plus a continuation `attribute(Energy > 0)` | by the effect's own period; ends the ability through continuation; shown in the Workbench as "drain" |
| Cooldown | `cooldown { effect, start: on_start | on_commit(group) | on_end }` | once per activation, by record flag; never per pulse |

### 5.2 The commit algorithm and its guarantee

A commit of groups G (plus the cooldown, when this commit starts it):

1. **Resolve** every effect definition and the actor's `AttributeSet`. Missing →
   fail closed (`CostDefinitionMissing`, `CostAttributeMissing`).
2. **Constrain** shape at compile time: cost effects are Instant, `Add` only, on
   declared attributes, targeting the actor.
3. **Aggregate** deltas per attribute across all groups being paid.
4. **Validate** each attribute: `Base + Σdelta ≥ attribute minimum`. Validate
   capacities: free tag slots for the cooldown's granted tags; free effect-owner
   slots; actor alive.
5. **Apply** costs in declaration order, then the cooldown, through an apply path that
   cannot fail after step 4 (asserted in debug).
6. **Record** the paid groups (and pulse index) on the record.

**Guarantee:** within one ability pass, either every delta and the cooldown in the
commit are applied, or none is, and the result names the first failing check
(`CommitResult{Status, GroupKey, AttributeName, Needed, Available}`). A failed commit
ends the activation `CommitFailed` unless the op says `on_fail: continue`.

**On clients** a commit never applies. It evaluates steps 1–4 against the
owner-replicated attributes to choose the predicted path, and shows pending costs as a
display overlay keyed by `(activation key, group key, pulse)`, cleared when
authoritative state for that key arrives. Replay can never pay twice because nothing
was paid.

### 5.3 Exact cooldown timing

Effects store an absolute end in their time domain (`EndTick` for simulation,
`EndLocal` for actor time, §7). The lifetime pass at the end of tick T removes effects
whose end ≤ T + 1. A cooldown of D ticks started in the ability pass of tick N
therefore blocks admission in ticks N+1 … N+D−1 (and later in tick N's own pass), is
removed in the lifetime pass of tick N+D−1, and **first permits reactivation on tick
N+D**. Tests `CooldownBlocksTheTickBeforeItsEnd` and `CooldownAdmitsOnItsEndTick`.

---

## 6. Authority, prediction and presentation

### 6.1 Authority

The full ability pass runs where `IsSimulationAuthority` holds. Activation input rides
the command (§2.6), so the authority's pass walks the same edges the client saw.
`AuthorityOnly` operations, costs and grants execute only there.

### 6.2 Activation identity

`AbilityActivationKey { std::uint64_t CommandTick; AbilityId Ability; std::uint8_t Ordinal; }`,
where `Ordinal` counts starts of that ability on that actor within that command tick,
in edge order. Two machines walking the same edge list over the same grants produce
the same keys; a disagreement shows up as a key present on one side only, which the
journal handles (§6.8).

### 6.3 Three presentation channels

| Channel | Carries | Transport |
|---|---|---|
| **Sustained** | phase `present` entries (charge glow, tether beam, Overclock) | derived from the replicated record's phase, on every presenting machine |
| **Pulse** | `cue` ops inside `while` (automatic fire's muzzle and impacts) | derived from the replicated record's pulse counter and last hit |
| **Discrete** | every other `cue` op (charge release, single shots, tether attach) | `AbilityCueLog` ring |

All three are state that snapshots already carry; none adds a message type. Every
occurrence carries its simulation tick and is presented when the presenting machine's
interpolated timeline reaches that tick, so remote cues line up with remote poses
(remote entities are drawn `SnapshotInterval + 2` ticks behind).

### 6.4 Records linger after End

A short activation can begin and end between two publishes. So an ended record stays
in `ActiveAbilities` as a **lingering** record (end reason, final phase, final pulse,
last hit) for `ability.present_linger_ticks` (default 8, four publishes at the
default rates), then is cleared. The component holds 4 live and 2 lingering records;
a new live record that needs a slot evicts the oldest lingering one, counted.
Lingering records never take part in admission.

### 6.5 Guarantees, limits, and the comparison asked for in review 2

**Stated guarantees for a peer for which the actor is relevant:**

- *Sustained*: presented while the record is live and replicated; reconstructed on
  late join. Ends within one publish after the record ends.
- *Pulse count*: the counter is cumulative, so it is never lost while the record is
  live. It is lost for that peer **only if every publish during the record's linger
  window fails to reach it** (four consecutive lost or deferred snapshots at the
  defaults), or if the lingering record is evicted first.
- *Pulse impact positions*: only the **last hit per publish** is carried. At cadences
  above the snapshot rate, a remote peer presents fewer impact reactions than shots
  (one per publish interval). Muzzle occurrences follow the count.
- *Discrete*: an entry is lost for that peer **only if 8 newer ring entries on the
  same actor are created before any snapshot carrying it reaches the peer.** With
  pulses carried by the counter, ring traffic is low-rate (single shots, releases,
  attaches).

**Sizes and cost at the defaults** (30 Hz publish, 100 ms RTT, so an unacked change is
resent about 4 times): a ring entry is about 20 bytes
(`Seq u16, Cue index u8, record slot u8 + activation sequence u16, op index u8,
tick delta u16, point 6 B quantized, normal 2 B octahedral, target NetEntityId`);
a firing record's pulse and last-hit fields are about 12 bytes, sent once per publish
while they change. Continuous automatic fire therefore costs about 360 B/s per firing
actor per peer, independent of fire rate; a ring occurrence costs about 80 B once. A
16-peer session with every player firing continuously spends about 5.4 KB/s of each
peer's roughly 35 KB/s snapshot budget on pulses. Stage 9 measures these figures.

| Concern | Ring + counter (chosen) | `UnreliableSequenced` messages | `ReliableOrdered` messages |
|---|---|---|---|
| Delivery | resent until acked, within the stated bounds | once; each occurrence lost with packet loss, and **any out-of-order packet is dropped** as stale | guaranteed |
| Latency | ≤ one publish interval + transit; presented on the interpolated timeline anyway | transit only, but must still be delayed to the interpolated timeline to line up with poses | resend latency ≈ RTT on loss; head-of-line blocking |
| High-cadence fire | constant ≈ 360 B/s per actor per peer | ≈ 20 B × fire rate per actor per peer, plus header per datagram | window of 32 packets shared with all reliable traffic; unsuitable |
| Relevance, owner rules | existing zone interest and field rules | must be rebuilt per message | must be rebuilt |
| Late join | sustained reconstructed; old entries skipped | nothing old arrives; sustained needs its own path | old backlog would replay |
| Overflow | oldest ring entry or lingering record dropped, counted | none (sender never knows) | queue growth |
| Sequence wrap | u16 serial-number arithmetic; ring and per-record pulse deltas are tiny against the 32767 window | per-message sequence needed | channel-managed |
| Prediction dedup | by `(activation key, op key, pulse)` | same key needed | same key needed |

The ring and counter reuse every mechanism snapshots already have; messages would need
relevance, ordering and late-join handling rebuilt, and lose more under reordering.
This is the revised D14.

**Out-of-order and wrap.** Snapshots are newest-wins and the channels are cumulative
state, so an older snapshot cannot rewind presentation. Ring sequences and pulse
deltas use serial-number comparison.

**Late join.** A presenter seeing an actor for the first time baselines each live or
lingering record's pulse count (no catch-up shots), skips ring entries older than
`ability.cue_window_ticks` (default 30), and starts sustained presentation from the
current phase.

**Prediction dedup on the owner.** A predicted discrete occurrence is keyed
`(activation key, op key, pulse)`; an authoritative ring entry with the same key is
not presented again. For pulses, the owner presents
`max(locally presented pulses, authoritative pulses)` for each key, so pulses it
already showed are skipped and pulses the server ran beyond its guess appear late.
Nothing already presented is retracted.

### 6.6 Authorable presentation bindings

`cue.presentation` is a data subtype mapping cue tags to presentation:

```json
{ "type": "cue.presentation", "version": 1, "data": { "cues": [
  { "key": "k1", "cue": "cue.charge_release",
    "once": [ { "verb": "audio.play", "arguments": { "clip": { "asset": "audio/ctx_release.ogg" } } },
              { "verb": "anim.present", "arguments": { "target": { "port": "actor" },
                  "intent": { "tag": "weapon.charge.release" }, "lifetime": { "const": "impulse" } } } ] },
  { "key": "k2", "cue": "cue.charge_glow",
    "sustain": [ { "lease": "audio.loop", "arguments": { "clip": { "asset": "audio/ctx_hum.ogg" } } } ] } ] } }
```

`once` entries are cosmetic verbs; `sustain` entries are cosmetic leases owned by the
presentation of that record. Both compile through the existing binding compiler; the
compiler refuses gameplay-scope entries (`scope_mismatch`) and the dispatcher refuses
them at runtime. Ports: `actor`, `target`, `point`, `normal`, `seed`, `count` (pulses
in this occurrence). Presenters available today are audio (non-positional),
presentation animation, and anything a game registers; a particle system registers
`vfx.emit` and `vfx.attach` into the same catalogs when it lands (D17).

### 6.7 Remote cosmetic animation

`RequestAnimation` stays exactly as it is: authority issues, the predicting client
journals, everyone else is refused. Presentation reactions use a separate,
machine-local path owned by animation:

- `anim.present` (cosmetic / Presentation) writes `AnimPresentationRequests`, a
  per-entity component with 4 records, `impulse` or `fixed` lifetime only. It is never
  replicated (no codec), never journaled, and written on whatever machine presents.
- A rig layer may be declared `presentation: true` (a new optional field on
  `AnimRigLayer`, default false). Presentation layers are request-keyed and read **only**
  presentation requests; ordinary layers never read them.
- The rig binder rejects a behavior that carries root motion or a Gameplay-scope event
  on a presentation layer, so a presentation request cannot move a body or fire a
  gameplay marker.
- On a listen server or standalone, presentation requests are written the same way,
  and still never enter the authoritative `AnimRequestSet`.

Authoritative animation state (`anim.hold`, `anim.request`) and local presentation
reactions (`anim.present`) are therefore separate in storage, replication and
permission.

### 6.8 Prediction classification

Each operation's category comes from its catalog definition (§4.2); queries are pure
and run wherever an executing op or guard needs them. An ability's `prediction` level
caps what a client executes for its own predicted pawn:

| Level | Client runs its own record? | Executes |
|---|---|---|
| `none` | no; it waits for the replicated record | nothing |
| `presentation` (default) | yes | phases, transitions, locals, queries, `Presentation` ops, `cue`, `present`; commits as prediction only (§5.2) |
| `simulation` | yes | everything in `presentation`, plus `Simulation` ops (movement leases, `time.scale`, `anim.hold`) |

`AuthorityOnly` operations never execute on a client at any level. Reconciliation uses
a `PredictionJournal` extracted from `AnimRequestJournal` (guess, match by key,
confirm / refuse / adopt): a confirmed client record adopts the authoritative phase,
phase tick and pulse whenever they differ; a refused guess ends `AuthorityDenied` and
releases its predicted leases (replay corrects movement); an authoritative record the
client never guessed is adopted and presented from its state.

**CTX Primary (`presentation`).** On the press the client runs its own record: commit
predicts success and shows pending ammo; `physics.trace` runs locally; `cue.muzzle`
presents at once; `cue.impact` presents at the local hit if the guard passes;
`effect.apply` (AuthorityOnly) is skipped. The server runs the same record and applies
damage. The ring entries arrive with the same keys and are not presented again by the
owner; remote peers present them. If the server's trace disagrees, the owner keeps its
predicted impact (a cosmetic misprediction) and the damage is the server's.

**CTX Charge (`presentation`).** Charge glow (sustained) and the charge pose
(`anim.hold`, through the existing animation journal's predicted issue) appear on the
press; on release the client enters Fire, presents `cue.charge_release`, predicts the
commit and skips damage. If the server refused the start (out of ammo it did not know
about), the journal ends the client record `AuthorityDenied`, the glow ends, and the
pending ammo overlay clears.

**Overclock (`simulation`).** The client acquires its own `time.scale` lease
(Simulation) with `FromTick = T + 1`, so its movement is dilated immediately and replay
reproduces it. The drain (`effect.hold`, AuthorityOnly) runs only on the server;
Energy reaches the owner by snapshot. The client evaluates `continuation Energy > 0`
on the replicated value, which can only lag above the server's (the drain is
server-only), so a client never ends Overclock earlier than the server for lack of
Energy. The server's end at tick E arrives as an ended record and a lease entry with
`UntilTick = E + 1`; the journal ends the client record, the authoritative lease entry
replaces the predicted one, and replay from the snapshot applies the scale exactly up
to E (§7.2). A toggle-off press is the same edge on both machines and ends both at the
same tick. The predicted vignette (sustained presentation) ends when the client record
ends, at most one round trip late in the Energy case.

### 6.9 What else must replicate

| State | To | Why |
|---|---|---|
| `ActiveAbilities` (key, ability, phase index, phase tick, pulse, last hit, end reason; codec maps ids to wire keys) | all relevant | sustained and pulse channels, late join |
| `AbilityCueLog` | all relevant | discrete channel |
| `AbilityGrants`, `AttributeSet` | owner | HUD, predicted commit path |
| `GameplayTagContainer` | owner; plus tags declared `replicate: all` to everyone | prediction gates; remote animation rules that read tags |
| `ActorTimeLeases`, `ActorTime` | all relevant | remote animation rate; replay history |

Remote locomotion facts are D18 (§13).

---

## 7. Time domains and Overclock

### 7.1 Three domains

| Domain | Clock | Who uses it |
|---|---|---|
| **Simulation** | the fixed tick, never scaled | network cadence, command timeline, replication, anything authored `simulation` |
| **Actor** | each actor's local time, advancing `scale` ticks per simulation tick | that actor's movement and sweep, effects targeting it (default), its ability phases (default), its animation rate, its AI cadence |
| **Presentation** | frame time | camera, UI, interpolation of remote entities (tick space) |

World pacing (`time.timescale`) is separate and not part of AbilityKit (D12).

### 7.2 The actor time contract (engine-owned)

**Leases and effective ticks.** `ActorTimeLeases` holds up to 8 entries
`{LeaseOwner, ScaleQ16, FromTick, UntilTick}`. A lease acquired during tick T has
`FromTick = T + 1`; released during tick T, `UntilTick = T + 1` (open until then).
Ended entries stay for `kActorTimeHistoryTicks` (64, the command ring's depth) after
`UntilTick` and are then dropped; acquisition beyond capacity is refused
(`LeaseRefused`). Every producer of dilation holds such a lease: an ability's
`time.scale`, an effect declaring `time_scale`, a game system.

**Scale.** `Scale(t)` = product of the `ScaleQ16` of entries with
`FromTick ≤ t < UntilTick`, in owner-key order, integer Q16 with rounding at each
step, clamped to `[time.actor_scale_min, time.actor_scale_max]` (defaults 0.1 and 2.0;
hard limit 4.0). Deterministic and independent of storage order.

**Accumulation.** At the start of each tick, before the ability pass and movement,
`ActorTime { ScaleQ16, LocalQ16 }` takes `ScaleQ16 = Scale(T)` and adds it to
`LocalQ16`. Integer arithmetic, identical wherever the same entries exist.

**Replication and replay.** `ActorTimeLeases` and `ActorTime` replicate to all
relevant peers and are in the pawn's predicted set, so the entries *are* the history.
On reconcile, the client restores the authoritative entries from the snapshot, merges
its own still-unconfirmed predicted `Simulation` leases from the journal, and replay
computes `Scale(t)` for each replayed tick from those entries. Nothing is recorded per
command, so there is no stale local scale to replay: a server-originated lease the
client never predicted is applied in replay from its `FromTick`, and one the server
ended is applied exactly to its `UntilTick`. Ticks after the acked tick remain
prediction: a lease the server ends later is corrected by the next snapshot, like any
other server decision. Tests `ServerOnlyDilationIsReplayedFromItsEffectiveTick` (the
server applies an effect lease at T that the client never predicted; after the
snapshot the client's replayed trajectory equals the server's for the same commands)
and `ServerEndedDilationIsReplayedToItsUntilTick`.

**Movement and collision.** `StepFreeLocomotion`, `StepJump`, root-motion sampling and
the character sweep take the actor's scale. For `s ≤ 1` the character step runs once
with `dt × s`. For `s > 1` it runs `n = ceil(s)` substeps of `dt × s / n`, each
a complete character step (locomotion, jump, root motion, composition, sweep), so no
sweep moves the character further per step than an undilated character moves per tick:
collision, stair stepping, ground snapping and push impulses behave as at normal speed,
and cost grows linearly with `n` (bounded by the hard limit). Platform carry velocity
is divided by `s` before the scaled step, so a dilated actor on a moving platform is
carried at the platform's simulation-time speed. Gravity applies in actor time (a
slowed actor falls slower).

**Effects, phases, animation.** Effect durations and periods run in the target's
actor time by default (`time_domain: target`) or in simulation time, stored as
absolute ends (§5.3); periodic effects may tick more than once per simulation tick at
`s > 1`, bounded by the existing catch-up guard. Ability phases measure timeouts,
cadence and progress in actor ticks by default; a `while every N` at `s > 1` can pulse
up to `ceil(s)` times in one simulation tick, within the step budget. Animation reads a
per-entity rate through `AnimClock::For(entity)` and re-anchors playbacks when an
entity's scale changes (`StartTick = now`, `Offset = current clip time`); blendspace
phase advances by `rate × scale`.

**AI cadence.** No AI decision system exists yet. The contract it adopts: per-actor
decision intervals, reaction delays and perception timers are measured in actor
ticks (`ActorTicksSince(actor, mark)` over `LocalQ16`), while the systems themselves
stay on the fixed schedule; navigation queries are instantaneous and unaffected. A
deferred entry records this, triggered by the AI substrate (roadmap Track A item 7).

### 7.3 Exclusions, and the interactions that are tested (D13)

General rigid-body dilation is deferred (a body's velocities and gravity factor would
need per-step scaling that interacts with contacts). Within the initial scope these
interactions are specified and tested:

| Interaction | Behaviour | Test |
|---|---|---|
| Dilated character pushes a dynamic body | push impulses come from substep sweeps, matching an undilated push per step | `DilatedCharacterPushesABodyLikeAnUndilatedOne` |
| Dilated character on a moving platform | carried at the platform's speed | `DilatedActorRidesAPlatformAtPlatformSpeed` |
| Two characters at different scales collide | each sweeps in its own time against the other's current shape | `CharactersAtDifferentScalesCollideConsistently` |
| Undilated body strikes a dilated character | the body runs at simulation rate; the character responds in its own time | `BodyStrikingADilatedCharacterUsesSimulationRate` |
| Fast actor near thin geometry | substeps keep per-sweep travel bounded | `FastActorDoesNotTunnelThroughThinWall` |

Audio pitch has no API (dilated sounds play at normal rate); look input is never
dilated.

### 7.4 Overclock, authored

A Sustained Toggle: an `Active` phase holding a `time.scale` lease (on the actor, or
on an `entities` local filled by `physics.overlap`), an `effect.hold` drain with
continuation `Energy > 0`, sustained presentation, and a cooldown `on_end`;
`prediction: simulation`. No engine code names Overclock; which actors it scales and by
how much are content.

---

## 8. Asset schema

### 8.1 Stable keys (D15)

- New `DataFieldKind::StableKey` (appended). The generic data editor mints a fresh key
  on add and on duplicate for any field of that kind, and the compiler rejects
  collisions per kind per asset.
- Keys are 64-bit random values rendered as 16 hex digits, minted by one
  `MintStableKey` extracted from `EditorScene::MintPersistentId` into `editor/common`.
  Hand-authored keys may be any unique identifier.
- Phases, ops, transitions, locals, conditions, cost groups, grant entries and cue
  entries carry keys. Compiled tables map key → index; trace records carry keys;
  diagnostics resolve a key to its current JSON path when shown. On the wire, records
  and ring entries use indices into the compiled definition, which both ends share.
- Clip events reuse a deleted max key (`AnimationClipEventsDocument.cpp:103-118`);
  recorded in `docs/deferred.md`, to move onto `MintStableKey` when animation is next
  touched.

### 8.2 Subtypes (all version 1)

| Subtype | Contents |
|---|---|
| `ability.definition` | display, `input_default`, activation / continuation / cancel_on, owned_tags, cancel_tags, instances, locals, costs, cooldown, phases, prediction, lifetime |
| `effect.definition` | today's definition, durations in seconds with `time_domain`, `granted_abilities` (grant entries), `time_scale` (an actor time lease while active) |
| `gameplay.attribute_declarations` | name, min, max, default |
| `cue.presentation` | §6.6 |
| `gameplay.tag_declarations` (existing) | gains optional `replicate: owner \| all` (default owner) |
| `animation.rig` (existing) | `AnimRigLayer` gains optional `presentation` (default false) |

New fields on existing subtypes are optional with defaults, so existing files load
unchanged and versions stay at 1. `AbilityGrants` replaces `AbilitySet` in scenes; the
four template prefabs (all empty) migrate with the schema change.

---

## 9. The Ability Workbench

### 9.1 Layout

Asset rail (abilities, effects, cue presentations, with "granted by" and "used by"
marks) · overview header (effective binding for the selected fixture,
activation/continuation/cancel summary, costs, cooldown, prediction level, problem
badge) · **phase strip** (an authored Shoji document on `WorkspaceWindow::Interactive`;
cards are phases, rows are ops, connectors are transitions labelled with trigger and
guard) · **inspector** (`DataProperties` scoped to the selected element's key, refined
by an `IDataSubtypeAuthoring`) · **preview** (stage host image and test controls) ·
**deck** (phase spans, input edges, one attribute plot, operation results).

### 9.2 Interactions

| Workflow | How |
|---|---|
| Create from a recipe | `New… → Ability → {Blank, Instant, Held/Periodic, Charge/Release, Timed Movement, Sustained Toggle}`; writes an ordinary document |
| Phases | add, rename, delete, reorder by drag; deleting a phase that transitions target asks before rewriting them to `end` |
| Ops | insert from a palette of every registered verb, query and lease (grouped, searchable, with scope and prediction badges), reorder by drag, delete, duplicate (fresh keys) |
| Arguments | rows from the operation's schema; each offers constant / port / local; asset arguments accept drops; "New effect…" creates and opens an effect without leaving the ability |
| Guards and transitions | a guard chip on the op row opens a condition editor; required guards (`hit`, `entity … has`) are offered as one-click fixes on the problem |
| Locals | named, typed list; renaming updates every reference (by key) |
| Activation / continuation / cancellation | three labelled lists in the header |
| Binding | ability default in the header; grant overrides edited where the grant lives, previewed in the effective-binding view |
| Presentation | cue panel: `present` and `cue` entries per phase; the cue's presentation binding opens in the same workspace |

Selection, undo and drag targets address elements by key.

### 9.3 Diagnostics

Compiler problems publish to `ProblemList` with the element key and current path. The
strip badges the card and the row; the inspector shows the field note; clicking a
problem selects the element and focuses the field (the property sheet gains
focus-by-key; Data adopts it in the same change).

### 9.4 Live inspector

While previewing, or in PIE with an actor selected: the edge list the tick walked;
chosen binding and the candidates it beat; admission result and failing condition;
active phase with local and simulation elapsed ticks; locals; query results; cost
groups paid; cooldown state; verbs invoked and their admission; leases held; cue
occurrences by channel; end reason; prediction state (guessed / confirmed / refused /
adopted) and authority. All of it is read from `AbilityTrace` and the components.

### 9.5 Preview, with honest rewind

A private World on `FixedTickPreview` (extracted from `AnimationPreviewSession`, which
migrates onto it in the same stage), running the production schedule. The fixture pawn
has an `InputActionSourceRef` to a preview source; controls write action edges into it
(press, hold, release, tap ×N within one tick, interrupt, swap source), so the real
`AbilityInputResolve` runs. Other controls grant and revoke, edit attributes and tags,
place a target dummy, and apply a time-scale lease. Every control is recorded as a timed
script; **rewind is deterministic replay** from tick 0 to the cursor; acting at a past
tick truncates the script after it.

---

## 10. Compiler validation and diagnostics

One compile function per subtype, run at load, on every Workbench edit and at save.
`AbilityDiagnostic { Severity, Code, ElementKey, Path, Message }`.

| Code | Example | Severity |
|---|---|---|
| `schema` | `Fire › Apply damage: 'scale' expects a number` | error |
| `key_duplicate` | `two ops share key 'o_trace'` | error |
| `phase_unknown` | `Charge › on release: no phase 'p_fir'` | error |
| `unreachable` | `Recover can never be entered` | warning |
| `no_exit` | `Charge has no release, timeout, condition or end` | error |
| `zero_time_cycle` | `Fire → Recover → Fire can repeat within one tick` | error |
| `unguarded_maybe_empty` | `Impact cue: 'at' comes from Hit, which may not have struck anything; add 'when hit(Hit)'` | error |
| `unguarded_recipient` | `Apply damage: target must have sencha.attribute_set; add 'when entity(Hit.entity) has sencha.attribute_set'` | error |
| `local_type` | `Trace writes a hit into Charge (float)` | error |
| `verb_unknown` / `query_unknown` / `lease_unknown` | `no verb 'effect.aply'` | error |
| `scope_mismatch` | `cue.presentation › once: 'effect.apply' is a gameplay verb` | error |
| `presentation_layer` | `rig layer Anim.Layer.Reaction is presentation, but behavior 'Lunge' has root motion` | error |
| `cost_shape` | `Shot cost 'ammo_1' is not an Instant Add on the actor` | error |
| `cost_attribute` | `Shot: 'Ammo' is not a declared attribute` | error |
| `commit_repeat` | `Fire › while: commit of 'Shot' needs repeat: per_pulse` | error |
| `commit_twice` | `'Shot' (once) can be committed twice: Fire and Overheat` | warning |
| `binding_ambiguous` | `Input.PrimaryFire: Charge and Auto are exclusive at priority 100 with overlapping contexts` | error in one source, warning across sources |
| `observe_cost` | `observe binding on an ability with costs` | warning |
| `prediction_level` | `prediction: none, but the ability has pulse cues the owner will see a round trip late` | info |
| `marker_no_fallback` | `Charge waits on an animation event with no timeout` | error |
| `domain_mismatch` | `progress_ticks in actor time but timeout in simulation time on one phase` | warning |
| `capacity` | `9 local slots declared; limit 8` | error |

World-bound resolution (verbs, queries, leases, tags, actions, attributes, component
identities) is the owner-thread second stage and reports the same way. Messages name
elements by display name and key, never by array index.

---

## 11. Ownership and cleanup proof

Every lease is owned by `{AbilityActivationRef, op key}`. `End` releases the
activation's leases in reverse acquisition order through each provider's `Release`,
from the record's own lease list, so cleanup never needs the definition. Every provider
also ends leases whose owner record is no longer live when it next runs (the pull
rule). Release is idempotent per lease. Lingering records hold no leases.

| Scenario | Path | Test |
|---|---|---|
| Normal completion | `End(Completed)` releases in reverse order | `LeasesReleaseInReverseOnCompletion` |
| Explicit cancel | `End(ExplicitCancel)` | `CancelReleasesEveryLeaseOnce` |
| Interrupted input | `End(InputInterrupted)`; exit ops do not run | `InterruptedChargeNeverFires` |
| Grant revoked | sweep finds the record's `AbilityGrantId` gone → `End(GrantRevoked)`; other grants' records untouched | `RevokingOneSourceKeepsTheOther` |
| Actor destroyed | components gone; providers' pull rule ends owned effects, animation requests, sessions, time leases | `DestroyedActorLeavesNoEffectsOrTags` |
| Zone move | components move with the actor; effects follow their target (Stage 1 fix) | `MovedActorKeepsItsActivation` |
| Zone detach | sweep records `ZoneDetached` before partition destruction; pull rule for the rest | `DetachMidTetherReleasesTheSession` |
| Target destroyed | entity local reads empty → `target_lost` or guarded ops skip; tether provider ends a session whose anchor died | `TargetDestroyedEndsTether` |
| Hot reload | `End(HotReload)` from the record's lease list | `ReloadMidChargeLeavesNoHeldAnimation` |
| Authority rejection | journal refuses → `End(AuthorityDenied)`; predicted leases released | `RefusedPredictionReleasesBurst` |
| Resource exhaustion | `CommitFailed` with nothing applied; lease refused → `End(LeaseRefused)` | `FailedCommitAppliesNothing`, `RefusedLeaseEndsActivation` |
| Capacity overflow | `CapacityExceeded` at Start; nothing acquired | `FifthActivationIsRefusedWithReason` |
| Presentation requests | `AnimPresentationRequests` expire by lifetime; presentation leases end with the presenting record or its linger | `PresentationLeavesNoRequestsAfterLinger` |

---

## 12. Stages and dependencies

Each stage builds, passes `ctest --preset dev`, updates the docs it touches, records
ABI, format and protocol changes in its commit and the relevant doc, and commits on this
branch. The ten stages and their order are unchanged from revision 2; contents changed
by this revision are marked †.

| Stage | Depends on | Content | Exit |
|---|---|---|---|
| **1 Effect and query prerequisites** | — | fail-closed affordability; effects in absolute ticks; `EffectOwner`, `RemoveEffectsOwnedBy`; tags revoked from the instance; canonical modifier fold order; effects follow their target across zones; expiry without the all-entities scan; `PhysicsQueries` ignore filter | unpaid-cost regression fails before the fix; fold order equal across shuffled storage; zone move keeps tags consistent |
| **2 Input edges** † | — | ordered device log; per-frame attribution into pending action edges; tick records with edge lists and derived summaries; `Fired` never on interrupted (D2); wire edge lists, `CarryEdges` concatenation, overflow rule; protocol 7; existing tests updated; `ConsumeKeyPressed` removed | §15.1 |
| **3 Content vocabulary and identity** | 1 | `StableKey` + `MintStableKey`; `ability.definition`, `effect.definition`, `gameplay.attribute_declarations`, `cue.presentation` compilers; registries at Publish; `AbilityGrants` and binding table; tag `replicate`; C++ registration removed; prefabs migrated | accept and reject fixtures per subtype |
| **4 Catalogs** † | 3 | lease catalog and dispatcher; `AuthoredScope` and `AuthoredPrediction` on verbs and leases with runtime scope enforcement; engine leases `effect.hold`, `anim.hold`; verbs of §4.4 except `anim.present`; queries `physics.trace`, `physics.overlap` | a test module registers a predicate query and a lease; both compile into an ability with no AbilityKit change; `ScopeMismatchIsRefusedAtRuntime` |
| **5 Phased runtime** † | 2, 3, 4 | `ActiveAbilities` (live + lingering), `AbilityLocals`, `AbilitySystem` five-step tick with run-to-quiescence, conditions incl. `hit` and `entity`, guards, continuation, cancellation, commit, cooldown timing, `AbilityInputResolve` over edge lists, request queue, `AbilityTrace` | Primary, Automatic, Charge headless through a real input source; §3.7 tests; zero allocations per tick after warmup |
| **6 Workbench slice** | 5 | `FixedTickPreview` (Animation migrated), workspace, strip, inspector, palette from catalogs, recipes, locals, guards with one-click fixes, problem → field focus, live inspector, scripted preview with replay rewind, effective-binding view | Primary, Automatic, Charge authored in the real Kyusu shell; authoring tests for commands, cancel, undo/redo, reorder-stable selection |
| **7 Actor time domain** † | 1 (parallel with 5–6) | `ActorTimeLeases` with effective ticks and history, `ActorTime`, scale-aware character step with substeps, platform carry, replay from lease history, effect and phase domains, animation re-anchor, `ActorTicksSince`; `time.scale` lease | §15.4 and the §7.3 table |
| **8 Movement, presentation** † | 5, 7 | `movement.burst/tether/mode` leases and kernels shared with replay; sustained, pulse and discrete channels with linger and `AbilityCueLog`; `cue.presentation` runtime; `audio.play`, `audio.loop`; `anim.present`, `AnimPresentationRequests`, rig `presentation` layers | Lunge, Tether, Overclock headless; local presentation tests |
| **9 Network** † | 8 | codecs (`ActiveAbilities`, `AbilityCueLog`, grants, attributes, tags by declaration, `ActorTimeLeases`, `ActorTime`); activation keys; `PredictionJournal` extracted from `AnimRequestJournal`; prediction levels; Impulse-over-replication fix; transport loss-bound tests and bandwidth measurement | §15.5 |
| **10 Finish** | 9 | Loss Function module refreshed to the current ABI and the six abilities authored as its content; capacity and CPU measurements; `abilitykit.md` rewritten; `input.md`, `movement.md`, `networking.md`, `authored-api.md`, `animation.md` updated; D18 entry in `docs/deferred.md` | §15.6 |

---

## 13. Risks and tradeoffs

- **The input-layer change is broad**: capture, attribution, resolver, net codec and
  about 40 tests. Stage 2 is isolated and lands early so it is reviewed alone. Counts
  per action were rejected because they lose interruption placement and cross-action
  order.
- **Conditions with combinators** could grow into an expression language. Depth ≤ 3,
  no arithmetic, no variables, new condition kinds only with a fixture.
- **Leases as a fourth catalog** add one registration concept for module authors; the
  alternative (handles returned through verbs) breaks the verb contract.
- **Presentation loss limits are real.** Pulse impacts are one per publish interval
  remotely; ring entries and lingering records can be lost under the stated sustained
  loss. Stage 9 measures them.
- **Actor time excludes general rigid-body dilation and audio pitch**; substeps make
  scales above 1 cost CPU linearly for that actor.
- **A presentation animation path is a second request store.** It is deliberately
  narrow (local, cosmetic, presentation layers only) because the authoritative store
  cannot be written by presenting machines.
- **D18, remote locomotion facts.** Remote pawns do not receive velocity, grounded
  state or tags, so their locomotion animation is stale regardless of AbilityKit. The
  acceptance demonstrations of remote presentation cover ability cues, presentation
  animation and authoritative ability animation requests; they do not claim remote
  locomotion looks right, and the walkthrough records that limitation.
- **Rejected alternatives:** activation entities; every op a verb; a branching-block
  op language; unreliable or reliable cue messages (§6.5); scaling the global tick for
  Overclock; recording per-command time scales for replay (stale after server changes);
  letting presenting machines call `RequestAnimation`.

---

## 14. Owner decision ledger

| # | Decision | Status |
|---|---|---|
| D1 | Incremental, independently reviewable changes against the current codebase | Directed; one branch off `main`, a commit per stage |
| D2 | Interrupted releases are not successful release-fired actions | Approved |
| D3 | Provenance-aware grants with stable grant identities replace `AbilitySet` | Approved |
| D4 | Authored assets are the production path; native extension through shared operations and providers | Conditionally approved |
| D5 | Drop and trace requests for dormant actors | Approved |
| D6 | Hot reload cancels incompatible active abilities in v1 | Approved |
| D7 | Predict movement and cosmetic feedback first | Approved; refined as prediction levels (§6.8) |
| D8 | Overclock is time dilation through a reusable time domain | Approved direction (§7) |
| D9 | Data-authored cue presentation bindings | Approved direction (§6.6) |
| D10 | Explicit priorities, safe consumption, visible conflicts | Directed (§2.4) |
| D11 | Neutral engine fixtures first, then Loss Function content | Approved |
| D12 | World pacing stays separate from actor-local time | Approved |
| D13 | General rigid-body dilation deferred; initial-scope interactions documented and tested | Conditionally approved (§7.3) |
| D14 | Presentation transport: state-derived sustained channel, pulse counter with end linger, and a replicated ring for other discrete occurrences, with the bounds of §6.5 | Provisionally approved; Stage 9 must validate loss behaviour, bandwidth and occurrence identity, and no stronger guarantee is claimed than its tests establish |
| D15 | Stable keys, collision validation, fresh keys on duplication | Approved |
| D16 | `AuthoredScope` with strict gameplay/cosmetic isolation | Approved; enforced by compiler and dispatcher (§4.2) |
| D17 | VFX independent; authoring contract ready for it | Approved |
| D18 | Remote locomotion facts tracked outside AbilityKit; effect on acceptance stated | Approved (§13) |
| D19 | Animation gains `anim.present`, `AnimPresentationRequests` and rig `presentation` layers (§6.7) | Approved; the separation from root motion and gameplay events is enforced |

---

## 15. Tests and acceptance

### 15.1 Input

`ReTapWhileHeldProducesReleaseThenPress`, `TwoTapsInOneWindowAreFourOrderedEdges`,
`InterruptedThenGenuineReleaseKeepsTheirOrder`, `CrossActionOrderIsPreserved`,
`TapsAcrossZeroTickFramesAreAllDelivered`, `CatchUpBurstPutsEveryEdgeOnTheFirstTick`,
`TwoControlsOnOneActionOrderEdgesExactly`, `ContextChangeAttributesTransitionsByFrame`,
`ContextLossIsInterrupted`, `FocusLossIsInterrupted`, `ProfileLossIsInterrupted`,
`InterruptedFallNeverFiresAReleasedModeAction`, `EdgeOverflowNeverInventsAStartOrARelease`,
`CommandCarriesOrderedEdges`, `CollapseConcatenatesEdgesInOrder`,
`StarvationRepeatCarriesNoEdges`, `ServerFeedWalksTheClientsEdges`.

### 15.2 Bindings and grants

`SameAbilityTwoPawnsTwoActions`, `GrantOverrideReplacesEveryBindingField`,
`BlockedExclusiveConsumesByDefault`, `FallbackIsOnlyByAuthoring`,
`EqualPriorityExclusivesStartNothingAndTrace`, `ObserveDoesNotConsume`,
`ReleaseRoutesToTheStartingActivationAfterModeSwitch`, `RevokingOneSourceKeepsTheOther`,
`GrantIdsSurviveReorderAndRemoval`, `BotSourceUsesTheSameResolver`,
`PossessionChangeEndsInputLost`.

### 15.3 Runtime

`ThreePressesOfAnInstantAbilityActivateThreeTimes`,
`PressesBeyondALiveInstanceAreRefusedAndConsumed`,
`ReleaseIsProcessedBeforeTheSameTicksPulse`, `GenuinePressThenInterruptCompletesAnInstant`,
`ChargeCapturesProgressAtRelease`, `EarlyReleaseRecoversWithoutCost`,
`MissSkipsDamageAndImpact`, `WorldSurfaceHitShowsImpactWithoutDamage`,
`HitOnEntityWithoutAttributesIsNotDamaged`, `HitScalesDamageByCapturedCharge`,
`StaleTargetSkipsClosed`, `ContinuationFailureEndsWithItsKey`,
`ActivationContextIsNotContinuation`, `StunCancelsAttackNotBuff`,
`CommitAggregatesCostsOnOneAttribute`, `FailedCommitAppliesNothing`, `OnceGroupPaysOnce`,
`PerPulseGroupPaysEachPulse`, `AutomaticStartsCooldownOnceAtEnd`,
`CooldownBlocksTheTickBeforeItsEnd`, `CooldownAdmitsOnItsEndTick`,
`MissingCostDefinitionFailsClosed`, `ZeroTimeCycleIsRejected`, `StepBudgetEndsTheRecord`,
`LingeringRecordsDoNotCountAsInstances`, `ModuleQueryGatesAnAbility`,
`ModuleLeaseIsHeldAndReleased`, `ScopeMismatchIsRefusedAtRuntime`,
`NoAllocationsPerTickAfterWarmup`, and the §11 table.

### 15.4 Time

`ActorScaleIsTheOrderedProductOfLeases`, `ScaleIsIdenticalAcrossLeaseInsertionOrders`,
`LeaseTakesEffectOnTheNextTick`, `SlowedActorCoversScaledDistance`,
`FastActorRunsCeilScaleSubsteps`, `FastActorMatchesSlowerActorOverEqualActorTime`,
`ServerOnlyDilationIsReplayedFromItsEffectiveTick`,
`ServerEndedDilationIsReplayedToItsUntilTick`, `EffectDurationRunsInTargetTime`,
`PhaseTimeoutRunsInActorTime`, `AnimationReanchorsOnScaleChange`,
`DilationNeverChangesTickCadence`, `RemovingALeaseRestoresScaleExactly`, and the §7.3
table.

### 15.5 Network and presentation

`ShortActivationPresentsRemotelyThroughLinger`, `AutomaticShotCountArrivesUnderLoss`
(loss pattern within the bound), `PulseCountLossBoundIsExact` (four consecutive lost
publishes after End lose the tail; three do not), `RingLossBoundIsExact` (the ninth
undelivered entry displaces the first), `ImpactsAreAtMostOnePerPublishInterval`,
`OutOfOrderSnapshotsDoNotDuplicateOrRewind`, `SequenceWrapPresentsInOrder`,
`LateJoinerSeesSustainedButNoOldShots`, `OwnerDoesNotPresentPredictedCuesTwice`,
`ServerExtraPulsesPresentLateOnTheOwner`, `RefusedPredictionEndsSustainedPresentation`,
`SameTickStartsGetDistinctKeys`, `DuplicateAndOutOfOrderCommandsStartOnce`,
`ImpulseRequestPresentsRemotely`, `TaggedFiringReplicatesToRemotes`,
`RemotePresentationAnimationNeverEntersTheAuthoritativeSet`,
`PresentationLayerRejectsRootMotionAndGameplayEvents`,
`CueBandwidthPerFiringActorIsMeasured` (records the figure; fails above the §6.5
budget), and each fixture under offline, listen, predicted-client and remote-client
variants.

### 15.6 Acceptance

| Item | Demonstration |
|---|---|
| Six abilities from shared mechanisms | Stage 8 headless fixtures + Stage 10 Loss Function content; no game-side state machines (grep gate on the module) |
| Reusable binding | `SameAbilityTwoPawnsTwoActions` + the effective-binding view on two prefabs |
| Native extensibility | the test module's predicate query and lease appear in the Workbench palette with no editor code (`ModuleOperationsAppearInThePalette`) |
| Correct charge | §15.3 charge tests; release presented remotely through the discrete channel (`ShortActivationPresentsRemotelyThroughLinger`) |
| Time dilation | §15.4 + the Overclock fixture; no Overclock symbol in the engine (grep gate) |
| Visual authoring | Stage 6 and 10 walkthroughs in the real Kyusu shell, recorded with what was and was not exercised live |
| Multiplayer | the §15.5 matrix, with the §6.5 loss limits stated in the report; remote locomotion quality excluded (D18) |
| Lifecycle | the §11 table |

---

## 16. Normative example: CTX Charge (`test/fixtures/content/abilities/charge_release.sdata`)

Keys are hand-authored here for readability; the editor mints 16-hex keys. Paths are
relative to the fixture content root. This file is Stage 5's compile fixture.

```json
{
  "id": "4b1e0c7d9a2f6e83",
  "type": "ability.definition",
  "version": 1,
  "data": {
    "display": { "name": "Charge shot", "category": "Weapon" },
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release",
                       "consume": "exclusive", "when_blocked": "consume", "priority": 100,
                       "on_interrupt": "cancel" },
    "activation": [
      { "key": "act_free", "tags": { "none": ["State.Stunned", "Weapon.Reloading", "Cooldown.Charge"] } } ],
    "continuation": [
      { "key": "cont_mode", "tags": { "all": ["Weapon.CTX.Mode.Charge"] } } ],
    "cancel_on": [
      { "key": "x_dead", "tags": { "any": ["State.Dead"] } } ],
    "owned_tags": ["Weapon.Firing"],
    "instances": 1,
    "locals": [
      { "key": "l_charge", "name": "Charge", "type": "float" },
      { "key": "l_hit", "name": "Hit", "type": "hit" } ],
    "costs": [
      { "key": "g_shot", "name": "Shot", "when": "commit", "repeat": "once",
        "effects": [ { "data": "effects/ammo_1.sdata" } ] } ],
    "cooldown": { "effect": { "data": "effects/charge_cooldown.sdata" },
                  "start": "on_commit", "group": "g_shot" },
    "phases": [
      { "key": "p_charge", "name": "Charge",
        "timing": { "domain": "actor", "progress_ticks": 60 },
        "hold": [
          { "key": "o_charge_anim", "lease": "anim.hold",
            "arguments": { "target": { "port": "actor" }, "intent": { "tag": "weapon.charge" } } } ],
        "present": [ { "key": "pr_glow", "cue": "cue.charge_glow" } ],
        "exit": [
          { "key": "o_capture", "op": "set", "local": "l_charge", "from": { "port": "phase.progress" } } ],
        "then": [
          { "key": "t_fire", "on": "release",
            "when": { "key": "w_min", "phase_ticks": { "at_least": 6 } }, "to": "p_fire" },
          { "key": "t_early", "on": "release", "to": "p_recover" } ] },
      { "key": "p_fire", "name": "Fire",
        "enter": [
          { "key": "o_commit", "op": "commit", "groups": ["g_shot"], "on_fail": "end" },
          { "key": "o_trace", "op": "query", "query": "physics.trace", "into": "l_hit",
            "arguments": { "origin": { "port": "aim.origin" }, "direction": { "port": "aim.direction" },
                           "range": { "const": 80.0 }, "ignore": { "port": "actor" } } },
          { "key": "o_damage", "op": "invoke", "verb": "effect.apply",
            "when": { "key": "w_recipient",
                      "entity": { "of": { "local": "l_hit", "field": "entity" },
                                  "has": ["sencha.attribute_set"] } },
            "arguments": { "target": { "local": "l_hit", "field": "entity" },
                           "effect": { "data": "effects/charge_damage.sdata" },
                           "scale": { "local": "l_charge" } } },
          { "key": "o_release_cue", "op": "cue", "cue": "cue.charge_release" },
          { "key": "o_impact_cue", "op": "cue", "cue": "cue.impact",
            "when": { "key": "w_struck", "hit": "l_hit" },
            "at": { "local": "l_hit" } } ],
        "then": [ { "key": "t_recover", "to": "p_recover" } ] },
      { "key": "p_recover", "name": "Recover", "timing": { "domain": "actor" },
        "then": [ { "key": "t_done", "on": "timeout", "ticks": 20, "to": "end" } ] } ],
    "prediction": "presentation",
    "lifetime": { "on_reload": "cancel" }
  }
}
```

What it guarantees, and the test for each:

| Behaviour | Why it holds | Test |
|---|---|---|
| Press begins charging | `press_release` starts on a genuine `Press` edge | `PressStartsCharge` |
| Progress accumulates deterministically | actor-time ticks, integer | `ChargeProgressIsTickExact` |
| Release captures the charge | `exit` runs on the transition, writing `phase.progress` into `Charge` before `Fire` enters | `ChargeCapturesProgressAtRelease` |
| Early release (< 6 ticks) | `t_early` → Recover; no commit on that path | `EarlyReleaseRecoversWithoutCost` |
| A successful release commits once | `g_shot` is `once`; the cooldown starts on that commit | `OnceGroupPaysOnce` |
| Commit failure | `on_fail: end` → `CommitFailed`; nothing applied, no trace, no damage | `FailedCommitAppliesNothing` |
| Miss | `physics.trace` answers `Unavailable`; `Hit` is empty; neither guard passes | `MissSkipsDamageAndImpact` |
| World-surface hit | `hit(Hit)` passes, so the impact presents; `entity(Hit.entity) has sencha.attribute_set` fails, so no damage | `WorldSurfaceHitShowsImpactWithoutDamage` |
| Hit on an entity without attributes | recipient guard fails; and `effect.apply` would refuse it anyway | `HitOnEntityWithoutAttributesIsNotDamaged` |
| Hit on a recipient | damage scaled by the captured `Charge` | `HitScalesDamageByCapturedCharge` |
| Mode switch mid-charge | continuation `cont_mode` fails → `ContinuationFailed`; exit does not run | `ModeSwitchCancelsCharge` |
| Menu, focus or possession change mid-charge | `InterruptedRelease` → cancel; source change → `InputLost`; exit does not run | `InterruptedChargeNeverFires` |
| Cooldown | `Cooldown.Charge` is blocked by `act_free` until the cooldown effect's end tick | `CooldownAdmitsOnItsEndTick` |
| Owner feedback | `prediction: presentation`: glow, charge pose and release cue present on the owner at once; damage and ammo stay the server's | `OwnerDoesNotPresentPredictedCuesTwice` |
| Remote presentation | glow from the replicated phase; release and impact through the discrete channel; the record lingers so a quick charge-and-release between publishes still presents | `ShortActivationPresentsRemotelyThroughLinger` |

Referenced fixtures: `effects/ammo_1.sdata` (Instant, `Ammo Add -1`),
`effects/charge_cooldown.sdata` (Duration 0.5 s, simulation domain, grants
`Cooldown.Charge`), `effects/charge_damage.sdata` (Instant, `Health Add -40`), and tag
and attribute declarations for every name used.

---

## 17. Implementation record

### 17.1 Corrections from the final decision, by stage

| # | Correction | Resolved in |
|---|---|---|
| 1 | CTX Charge: `anim.hold` is `Simulation` but the fixture is `prediction: presentation`; choose one policy and update the fixture and tests | Stage 5 (fixture) and Stage 9 (prediction tests) |
| 2 | Activation identity must not shift when client and server admit differently; tie it to the originating input attempt and its position, and test divergent admission in one tick | Stage 5 (key shape) and Stage 9 (divergence tests) |
| 3 | Retained time-lease history must not exhaust active lease capacity; test rapid acquire/release across the 64-tick window | Stage 7 |
| 4 | Stage 5 uses actor-time phase semantics before Stage 7 adds dilation: the actor clock (identity scale) lands in Stage 5 as the real clock; Stage 7 adds leases and scaling to it, replacing nothing | Stage 5 |
| 5 | A saturated edge buffer must not invent a start or genuine release, leave an activation held, or corrupt the wire; synthetic cleanup works even when full | Stage 2 |
| 6 | Discrete occurrences stay interpretable after their lingering record is evicted; test several instant shots in one tick | Stage 8 (ring entry content) and Stage 9 (tests) |

### 17.2 Stage 1 — effect and query prerequisites

Deviations from the plan's wording, each for a stated reason:

- **Effects count down instead of storing an absolute end** (§5.3 said "absolute
  end"). Absolute ends would let a dormant target's effects expire while its zone
  sleeps; today they pause, and that contract is kept. The timing guarantee is the
  same: D ticks applied on tick N first permit tick N + D. A countdown also extends
  directly to actor time in Stage 7 (count down by the actor's scale).
- **Effects live in the persistent partition** instead of following their target
  between zones (§12 "effects follow their target across zones"). Following would
  need the zone runtime to know about effects, and a detach in the same frame as a
  move could still strand tags. In the persistent partition nothing is stranded:
  an effect is gated on its target's partition being active, and ends when its
  target or owner is gone.
- **The cost floor is the higher of zero and the attribute's minimum**, not the
  minimum alone: an attribute declared without a minimum would otherwise make any
  cost affordable.

### 17.3 Stage 2 — ordered input edges

- **Attribution replaces the latch.** The bitset latch is gone. Once per frame,
  after contexts settle, `AttributeInputFrame` replays the frame's transition log
  under that frame's contexts and claims (both depend only on which contexts are
  active, so the replay is exact) and appends action edges to both clocks. A single
  `BindingHeld` rule serves attribution and resolution, so edges and the held flag
  cannot disagree.
- **Correction 5 (overflow).** A pass's kept edges are walked from the previous
  pass's held state; if they end held while the action is up, the pass publishes an
  interrupted fall in its summary flags (and in the list if there is room), and if
  they end up while the action is down it is held with no press. The summary flags
  travel on the wire beside the list, so a reconstructed record is never wrong
  about what is held. Tests: `AnOverflowNeverInventsAStartOrAReleaseAndNeverStrandsAHold`,
  `OverflowDropsTransitionsButKeepsHeldStateExact`.
- **Fire modes on lost profiles.** The clock no longer carries each action's fire
  mode for a release owed after the profile is gone: those falls are interruptions,
  which never fire.
- **Pause.** On resume the simulation clock drops what was made during the pause
  but keeps interruptions (`DiscardSuspendedInput`).
- **Wire.** Five flag bits per action, then a 6-bit edge count and 7 bits per edge.
  `kNetProtocolVersion` 7.
- **Deviation:** the plan said per-action edges alternate; they need not, because a
  silent rise is not an edge. Recorded in §2.6.

### 17.4 Stage 3 — content vocabulary and identity

- **Reorganised.** Ability definitions and grants (`ability.definition`,
  `AbilityGrants`, the binding table, removing C++ ability registration, the prefab
  migration) move to Stage 5, beside the runtime that executes them: moving them
  here would have needed a temporary interpreter for the old single-shot runtime,
  which correction 4 and the constitution both rule out. `cue.presentation` moves to
  Stage 8 beside its runtime, and the tag `replicate` field to Stage 9 beside the
  codec that reads it.
- **Delivered.** `DataFieldKind::StableKey` (validated, unique among an array's
  elements); editors mint keys on add (`CreateDefaultDataValue`) and re-mint them in
  a duplicate (`RemintStableKeys`), through one `RandomKeySource` that scene
  persistent ids also use. `gameplay.attribute_declarations` and
  `effect.definition` subtypes; `DeclareContentVocabulary` registers tags,
  attributes and effects into the runtime World at publish and into every editor
  document; effects reload in place under the same id.
- **Found and fixed in passing:** level documents never declared content tags, so a
  tag only content named could not be read back by a document.

### 17.5 Stage 4 — catalogs

- **Deviation: a lease is a verb kind, not a fourth catalog.** §4.3 described an
  `AuthoredLeaseDefinition` catalog and a `LeaseDispatcher` beside the verb
  dispatcher. Acquiring a lease needs every check `Invoke` makes (binding,
  revision, argument filling, persistent-entity resolution, data-asset subtypes,
  reentrancy, trace), so a second dispatcher would have been a second copy of that
  path. `VerbDefinition` gains `Kind { Instant, Lease }`; `AcquireLease` and
  `ReleaseLease` sit on `VerbDispatcher` and share its one dispatch path; a binding
  compiles the same way for either kind and the producer chooses which call it
  makes, each refusing the other (`KindMismatch`). The palette, the binding
  compiler and the editor vocabulary listing need nothing new to show a lease.
- **Delivered.** `AuthoredScope` and `AuthoredPrediction` on every verb, in the
  contract, validated against each other; runtime scope enforcement
  (`ScopeMismatch`, `ScopeMismatchIsRefusedAtRuntime`); `LeaseOwner` in `ecs/`,
  replacing `EffectOwner` and now also the owner of a Held animation request;
  `effect.apply`, `effect.hold`, `anim.hold`; `anim.request` and `anim.cancel`
  reclassified Simulation; `physics.trace` and `physics.overlap` bound in any
  host whose game registered physics; `DeclareEngineVocabulary` declaring verbs and
  queries together. A miss is an answer (`hit = false`), not `Unavailable`, as §4.1
  specifies.
- **Duplication removed in passing.** `AnimEventScope` was the same
  classification as `AuthoredScope`; it is now that type, with the cooked values
  kept, and an event passes its scope with every invocation. A cosmetic event bound
  to a gameplay verb or a lease is reported at bind
  (`anim.event.scope_mismatch`). The engine's per-verb binding tokens became one
  list.
- **Moved.** `ability.activate`, `ability.cancel`, `ability.grant` and
  `ability.revoke` land in Stage 5 with the request queue and grants they operate
  on; `audio.play` lands in Stage 8 with `audio.loop` and the other presentation
  operations. The exit test "both compile into an ability" needs Stage 5's
  compiler: the test module now declares `spike.grapple.tether` (a Simulation
  lease) and `spike.grapple.in_range` (a Bool query), and Stage 5's compiler tests
  compile them into an ability.
- **Stage 5 owes the animation pull rule.** An `anim.hold` whose owner's entity is
  gone is still reported as orphaned, the existing rule for any Held request a
  producer forgot. §11's "providers end leases whose owner is gone" lands with
  `DestroyedActorLeavesNoEffectsOrTags`, where the orphan report is reconciled with
  it. Effects already end such holds.
- **Zero-allocation note for Stage 5.** Query results that are records or arrays
  allocate per evaluation (`AuthoredValue::Record`, `Array`). Stage 5's exit
  measures allocations per tick and fixes this if a trace or overlap shows up.
- **Formats and protocol.** No persisted format changed (`AuthoredScope` keeps
  `AnimEventScope`'s values). The replication table hash moved because
  `AnimRequestSet`'s local size grew; the wire record did not change, and protocol
  7 from Stage 2 covers it. ABI v26 history extended.
- **Deferred.** Declaring leases and cosmetic or predicted verbs through
  `SENCHA_VERB` (`docs/deferred.md`).

### 17.6 Stage 5 — phased runtime

- **Delivered.** `ability.definition` with a World-free compiler (structural
  diagnostics) and a World-bound resolver (names, operation contracts, recipient
  guards against each argument's declared component); abilities declared from
  content after effects and redefined on reload. `AbilityGrants` with per-actor grant
  ids and the World's `AbilityGrantEntries` (key, override, effective binding); the
  scene form and editor adapter; the four template prefabs migrated. `ActiveAbilities`
  (live and lingering records), `AbilityLocals`, the actor clock at identity scale
  (correction 4: Stage 7 adds leases and scaling to `ActorTime`, replacing nothing),
  the five-step pass with run-to-quiescence, conditions, guards, continuation,
  cancellation, atomic commit and cooldown timing, input resolution over edges,
  the request queue and `ability.*` verbs, and `AbilityTrace`. A warm pass allocates
  nothing (measured: `AWarmPassAllocatesNothing`).
- **Correction 1.** The CTX Charge fixture is `prediction: simulation`: its
  charge pose is an `anim.hold`, a Simulation lease, and the policy is that an
  ability whose presentation includes held authoritative animation predicts at the
  simulation level. Stage 9 carries the prediction tests.
- **Correction 2.** An activation's key is `{command tick, ability, origin}`, the
  origin being the press's index in the tick's edge list (or `0x80` plus the
  request's place in the queue): it names the attempt, not a count of admitted
  starts, so a refusal on one machine does not shift the keys of later starts.
- **Deviations.** The input vocabulary reaches abilities through
  `InputActionVocabulary`, which the resolve pass publishes, rather than through the
  binding cache. `when_blocked` is `consume | fall_through`; the separate
  `fallback` flag of §2.2 would have said the same thing twice. An actor needs an
  `InputActionSourceRef` to read input (the input layer's existing rule). Kit systems
  are registered by the engine for every host before the game's hook, as
  animation's are, so they can be handed the dispatchers; games no longer call
  `RegisterAbilityKitSystems`. The fixture lives in its own content root,
  `test/fixtures/abilities/assets`, with `asset://` paths.
- **Scoped to later stages, with their consumers.** Transitions `on: complete`
  (a lease reporting completion) and `cancel_on` entries for `target_lost` and
  authored events arrive with the movement leases that need them (Stage 8); an
  op's `cue` is traced and reaches presentation in Stage 8; a phase's `present` is
  stored for Stage 8's sustained channel. The transition `reason` field is not
  offered: every transition to `end` ends `Completed`.
- **Effect-sourced grants.** `granted_abilities` on `effect.definition`, in the
  grant-entry form scenes use, recorded per effect in `AbilityEffectGrants`. Deviation
  from §2.2: rather than effects granting and revoking abilities inside
  `ApplyEffect`, which would make effects depend on abilities, the ability pass
  brings each actor's effect-held grants into step with its live effects at the start
  of every pass. A grant therefore appears the tick after its effect applies and goes
  the tick after it ends, identically on every machine.
- **Owed.** The animation pull rule (§17.5) is recorded in `docs/deferred.md`.

### 17.7 Stage 7 — actor time

- **7a delivered.** `ActorTimeLeases` with active and historical room kept apart
  (correction 3: 8 active, 16 history; history evicts its oldest, counted, and never
  refuses a new lease; `HistoryNeverUsesTheRoomANewLeaseNeeds` runs 40 rapid
  acquire/release cycles inside the 64-tick window), `ActorScaleAt`, the clock driven
  from leases with the pull rule, `time.actor_scale_min/max`, the `time.scale` lease,
  effects counting in their target's time (`time_domain`) and an effect's own
  `time_scale`, ability phases on the actor's clock, `ActorTicksSince`.
- **Deviation: product order.** §7.2 orders the factors by owner key. Owner keys
  carry entity indices and per-actor start counts, which two machines need not
  share; ordering by the scale values themselves gives every machine the same
  product from the same set of leases, which is what the ordering is for.
- **7b delivered.** Characters own a clock; `ActorTimeDilated` routes a dilated
  character out of the bulk movement systems into `DilatedCharacterStepSystem`, which
  runs the replay kernel with substeps and scaled platform carry; replay takes each
  tick's scale from lease history (`AServerOnlyDilationIsReplayedFromItsEffectiveTick`,
  `AServerEndedDilationIsReplayedToItsUntilTick`). The dilated step runs in the fixed
  phase, as replay does, rather than beside the bulk drive in the physics phase.
  Found in passing: `Archetype::BuildLayout` sized chunks without the padding between
  aligned columns, so some archetypes overflowed their chunk; fixed with a regression
  test.
- **Deviation: effect leases end a tick late.** An effect's `time_scale` lease is
  held by the effect entity and ended by the clock's pull rule the tick after the
  effect ends, rather than by `EndEffect`, which has no tick to stamp it with.
- **7c delivered, in part.** `ADilatedCharacterPushesABodyAtTheSpeedItCrossesTheSimulation`
  and `FastActorDoesNotTunnelThroughThinWall` run against Jolt; the platform case is
  7b's `ADilatedActorRidesAPlatformAtThePlatformsSpeed`.
- **Deviation: two §7.3 interactions do not exist.** The character mover has no
  character-versus-character collision and no inner body, so characters pass
  through each other and bodies through characters at every scale;
  `CharactersAtDifferentScalesCollideConsistently` and
  `BodyStrikingADilatedCharacterUsesSimulationRate` wait on that
  (`docs/deferred.md`, "Dilated characters meeting characters, and bodies striking
  them").
- **Deviation: the push test asserts what physics does.** §7.3 expected a dilated
  push to match an undilated one. Bodies run at simulation rate, so a slowed
  character kicks a crate at the speed it crosses the simulation and the crate runs
  on less far; the test asserts that. Found in passing: an undilated character also
  walks through a 2 kg crate after shoving it, so a character never stops at a light
  dynamic body. That is existing mover behaviour, not dilation's.
- **7d delivered.** `AnimPlayback` carries its actor's `TimeScale` and a retime
  anchor beside its identity; content resolution re-anchors at the tick before a
  change (`AnimationReanchorsOnScaleChange`), so event passes cross each mark once
  across changes (`PassesAcrossChangesOfScaleCrossEachMarkOnce`); blendspace phase,
  flow sections and the pose pass's phase rate advance by `rate × scale`. Root motion
  is sampled once a tick at the simulation tick length, per second of actor time,
  with its turn shared among substeps (`ASlowedCharacterIsCarriedTheWholeCurveInItsOwnTime`,
  `AHastenedCharacterTurnsAsFarAsTheCurve`); before, a dilated step sampled it per
  substep at the substep's length. Replay's per-tick scale moved to
  `ActorScaleOnTick`, which animation reads too.
- **Deviation: no `AnimClock::For` rate.** §7.2 has animation read a per-entity rate
  through `AnimClock`. The scale is read from the leases for the tick the entity is
  on (`ActorScaleOnTick`), the function replay already used, so the clock stays a
  tick and live, replayed and re-resolved ticks agree.
- **Limit: re-anchoring needs the live sequence.** A playback that begins in the
  past (a request issued before the layer saw it, a follower reconstructing a
  flow from its anchor) is timed from its start at the current scale; ticks before
  the latest anchor are extrapolated at the current scale. Stage 9 replicates the
  leases and `ActorTime`, and decides whether followers need more.

### 17.8 Stage 6 — Workbench

- **6a delivered.** `FixedTickPreview<Record>` (`editor/common/src/preview/`) holds the
  clock the Animation preview ran on -- tick zero from a rebuild, stepping, speed
  that schedules ticks without changing their length, a bounded history, rewind as
  a replay from zero, one script branch per paused tick -- and
  `AnimationPreviewSession` runs on it (`docs/editor/fixed-tick-preview.md`).
- **6b delivered.** The Abilities workspace (`editor/ability_editor/`,
  `docs/editor/ability-workbench.md`): an element model naming every keyed element
  by key and current path; key-addressed edits applied as one journal step each;
  checks (compile, then resolve against a `VocabularyCatalog` that now also holds
  the kit's registries and the content vocabulary, and lists each verb's kind,
  scope and prediction); problems named by element and revealed as the element
  selected with its field focused (`WorkspaceSelectionFacet::Focus`; the sheet
  opens the groups holding it, marks and scrolls to its row); the card as
  `DataProperties` scoped to the selected element's path; recipes through
  `DataCreation` (documents can now be created with their data); the Abilities and
  Elements explorers. Found in passing: `JsonObjectEdit.h` moved from the Data
  workspace to `editor/common/src/data/` at its second consumer.
- **6c delivered.** The phase strip on the window's interactive surface: phases as
  cards holding their op lists and transitions, guard text, problem lamps, picking
  by key, dragging phases and ops (`DropAbilityElement`; RmlUi sends drops only for
  `drag: drag-drop`), and the Operations palette over the catalog with scope and
  prediction badges (`AuthoredPredictionName` is new) and placement rules (a lease
  only ever held).
- **Deviation: transitions are rows, not drawn connectors.** Each card ends with
  its transitions as rows ("on release → Fire", with the guard under it). Drawn
  connectors between cards add nothing a row does not say and would need geometry
  the document cannot lay out; the count of transitions coming in is on each card.
- **Deviation: no Timed Movement recipe yet.** It is built on `movement.burst`, which
  Stage 8 adds; the recipe joins the table with that lease.
- **6d delivered.** Argument rows from each operation's declared contract
  (constant, port or local by kind; an effect argument's "New effect…"); one-click
  guard fixes for `unguarded_maybe_empty` and `unguarded_recipient`, joined to an
  existing guard; the overview card with the effective binding. The runtime's
  binding merge is now `EffectiveAbilityBinding`, shared with the overview; the
  port, local-type and hit-field name tables moved to `AbilityValueNames.h`, read
  by the compiler and the editor.
- **Deviation: the effective binding shows the ability's default.** A grant's
  override lives where the grant does (a scene, an effect); the overview shows the
  binding with nothing overriding it, and the preview's fixture grant (6e) shows
  its own.
- **Remaining in Stage 6.** The preview on `FixedTickPreview` with a scripted
  fixture, rewind by replay, the live inspector and the deck.
