# Animation Runtime

Status: **current architecture** (2026-09). This documents the animation
runtime as it exists in the tree: `engine/include/anim/` and `engine/src/anim/`.
The editor that authors it is described in `editor/animation_editor/README.md`.
The design rationale and its invariants are in the plan of record,
`docs/plans/animation-runtime.md`; the staging is `docs/plans/animation-authoring.md`.
Runtime tests live in `test/runtime/Anim*Tests.cpp`, and the editor's in
`test/editor/Animation*Tests.cpp`.

## The model

Animation is stateless selection plus named memory. There is no state machine
and no transition graph.

- **Facts** are gameplay values the rig reads: speed, grounded, a stance tag.
  `AnimFactGatherSystem` copies them from the entity through registered
  providers into `AnimFacts` or `AnimFactsLarge`. Every fact is computable with
  animation compiled out.
- **Requests** are gameplay asking for a presentation intent: a reload, a door
  opening. They live in `AnimRequestSet`, are held, fixed-length or a single
  tick, and carry a start tick every machine agrees on.
- **Selectors** pick, every tick and for each layer, the behavior the facts and
  requests call for. A rule names a behavior, never another rule.
- **Behaviors** are gameplay tags with a policy: kind (cyclic, one-shot, flow,
  hold), blend, latch, speed and start, late-join handling and lifecycle
  bindings.
- **Slot maps** turn a behavior into content: a clip, a blendspace or a flow.
  Only slot maps name content.
- **Layers** stack, masked by bone and weighted by rule; `AnimPoseSystem`
  composes them.
- **Events** from clip markers and behavior or flow lifecycle bindings invoke
  authored verbs, as `docs/gameplay/authored-api.md` describes.
- **Root motion** is a curve inside the clip. `RootMotionSystem` reads it
  through `RootMotionSource`, so movement moves the character, not animation.

Each fixed tick runs gather, select, content resolution (`AnimContentSystem`)
and events, in that order; root motion follows content resolution, and the pose
is composed from what content resolution settled.

## Content

| Asset type | Holds |
| --- | --- |
| `animation.rig` | Skeleton, fact and request schemas, layers, and the lists of behavior sets, slot maps, blend overrides and bindings |
| `animation.fact_schema` | Fact slots and derivations over them |
| `animation.request_schema` | Intents and their parameters |
| `animation.behavior_set` | Behaviors and their policies |
| `animation.selector` | Rules for one layer |
| `animation.slot_map` | Behavior to content rows |
| `animation.flow` | Sections with loops, branches and a cancel section |
| `animation.blendspace` | Clips placed over one or two facts |
| `animation.blend_overrides` | Pairwise blend policies, capped per rig |
| `gameplay.tag_declarations` | Names content uses as gameplay tags |

Every name in these assets (behaviors, intents, layers, sections) is a
gameplay tag. Content stores names; ids are registration order and are
resolved when a rig binds. A rig's names are declared in a
`gameplay.tag_declarations` asset beside it, which `RuntimeContent` registers
when content is published, in asset-path order, before anything binds. A name
nothing declares fails the binding with a diagnostic rather than becoming a
new tag.

### Tiers

A tier is which components an entity carries, not a type:

- **Prop**: `AnimRig`, which brings `AnimRequestSet`, `AnimContentState` and
  `AnimFlowState`. Requests alone choose what plays.
- **Simple**: a Prop plus `AnimFacts`, which brings `AnimFactHistory` and
  `AnimSelectorState`, so selectors run.
- **Character**: `AnimFactsLarge` instead, usually with `AnimDecisionLog`.

Measured per-entity footprints are 2000, 3648 and 8456 bytes, the last
including a decision log (`AnimWorldReport.FootprintsAreTheComponentsEachTierCarries`
records them). A clip played on its own is a one-layer rig whose behavior sets
its speed and start; the animation editor's Migration panel converts scenes
that still name the clip player component.

The rig does not declare its tier: a rig without a fact schema is a Prop rig.
Capacities (layers, requests, request parameters, fact slots, cooldown slots)
are architecture rather than tuning. Per-entity state is arrays of known size,
so a rig that needs more has to be restructured, not a constant raised.

### On a machine that presents no pose

A headless server, or any host registered with `AnimationHost::PresentsPose`
false, does not register the pose pass and produces no cosmetic events. It also
skips gathering, selection, content resolution and events for every rig whose
binding proves its animation cannot reach gameplay. `AnimBoundRig::DrivesGameplay`
records the proof and `ShouldRunAnimationLogic` is the one test the systems make.

A rig drives gameplay when it plays a flow (the authority stamps section anchors
late joiners reconstruct from), has a root-motion behavior, or carries a
Gameplay-scope clip event or lifecycle binding. Anything the binding cannot see
into counts the same way: an invalid rig, or a clip that is not loaded and so
has unknown events. A false positive costs animation work; a false negative
would silence gameplay, so uncertainty runs the rig. The flag is rederived on
every rebind, so a rig moves between the two as its content changes.

Root motion needs facts, selection and content resolution, since
`SampleAnimRootMotion` reads the base layer's resolved content or its request;
the movement-side `RootMotionSystem` always runs. A root-motion rig is never
skipped, so a headless authority carries its character. Cosmetic rigs also skip
the authority's timing stamp, which only lets a client flag a timing mismatch.
The skip roughly halves a headless tick over cosmetic props (see Measurements);
what remains is visiting each entity and resolving its rig.

## Binding

A rig asset and the assets it lists only name things. `AnimRigBindings`, a
World resource, binds each rig into one World: the fact schema chain merged
into one fixed layout, derivations compiled to slot indices, layer and intent
names resolved to the World's tag ids, gathered slots matched to the World's
providers, each layer's selector flattened and compiled, behaviors resolved to
their policies, the slot map stack merged into rows over a content table, and
every clip event bound to the rig's authored bindings. Per-entity state is
addressed by the indices the binding produces.

Every problem with the content is a located `AnimDiagnostic`. A rig with an
error binds as invalid, and its entities gather nothing rather than run half a
layout. The binding is derived state: it is rebuilt when an asset it read
reloads, a clip it plays is replaced in place, the tag vocabulary grows, a fact
provider is bound, the verb catalog changes, or the blend override cap
changes. No component holds a pointer into it: per-entity state records the
binding generation it was taken against, and remaps by stable key or resets
when that generation moves.

## Facts

A fact snapshot is one 32-bit value per declared slot, gathered once per tick
from gameplay and derived from other facts. Slots are addressed by the index
the rig's binding gave them; names live in the schema. There are two fixed
capacities, chosen per rig, as two components, `AnimFacts` and
`AnimFactsLarge`; an entity carries the one its rig asks for, or neither. A
tagset slot holds nothing, since tag predicates read the entity's
`GameplayTagContainer`; it reads as 1 when the entity has one.

`AnimFactHistory` is the bounded memory of the temporal derivations. Every
closed derivation op needs only its last transition inside its window (an
edge's tick, when a condition last held, a smoothed value), so each derivation
keeps one small record rather than a ring of raw samples. Every derived fact is
exact once the entity has been observed for one horizon, the longest window
in the schema, which `ObservedSinceTick` measures.

A fact schema declares the typed slots rules may read and the facts derived
from them. Slots are open: a game or mod may declare one by name. Derivation
ops are closed, so rule evaluation stays a fixed program over fixed slots. A
schema may extend another; the engine ships a core schema and a game's schema
extends it. The chain merges when the rig binds, base slots first, into one
fixed layout, and a name declared twice anywhere in the chain is a conflict,
not an override. Assets hold names only; slot indices and tag ids belong to
the binding.

Gameplay values reach fact slots through `AnimFactProviders`, a World resource
binding a slot name to a plain function over a const World. Adding an
animation-relevant value is one `Bind` or `BindField` call in game code, with
no engine edit, and gathering is a call per slot per entity with no allocation
and no virtual dispatch. A provider must produce the same value on a server
with animation compiled out: it reads gameplay components and nothing about
what is playing. One that needed to know would be selector or flow state.

`EvaluateAnimDerivations` runs a bound rig's derivations in declaration order,
so each reads values already final for the tick. It is pure over its
arguments, so the gather system, the preview and tests share it. The first
tick an entity is observed carries no prior value, so an edge cannot be seen on
it and a duration starts counting from it. Smoothing converges at the rate its
time constant sets rather than becoming exact.

## Requests

`AnimRequestSet` holds gameplay's intent toward animation as eight fixed
records. It is the only animation-facing data gameplay writes and the only
animation data that replicates. A request says "present this intent, from this
tick, on these layers, until I say otherwise". It carries no priority, target,
handler or reply: precedence lives in selector rules, and anything that looks
like a message back to gameplay is a verb travelling the wrong direction.

The set changes only through `IssueAnimRequest` and `CancelAnimRequest`
(`AnimRequests.h`), so an ability, a preview scenario and a replicated snapshot
all get the same outcome for the same inputs:

- expired Fixed and Impulse records, and cancelled records whose tail has
  ended, are pruned before any insert;
- a Held or Fixed request from the same source with the same intent as a live
  record supersedes it in place, which is how a combo advances;
- an Impulse is deduplicated per source, intent and tick, and never supersedes
  anything;
- with every record occupied the new request is rejected. Nothing is evicted,
  so server and client decide identically.

The rules are pure over the component data. The World overloads find the
components, validate the intent against the entity's rig and write the
decision log.

## Predicates

Selector enter and stay conditions, slot map rows and flow conditions share one
condition language. A predicate is rows that must all pass; a row is one test,
optionally negated, or an any-of group of tests. That is conjunctive normal
form, complete for boolean conditions, and it is what the schema validates,
what the editor builds and what "the first row that failed" explains.

A test reads a fact, the entity's tag container, a request, or how long the
layer's behavior has been winning. Nothing else is nameable: there is no operand
for a rule, a behavior, a previous winner or content, so no rule can become an
edge to another. Rows compile per World into a closed stack program
(`AnimProgram`) over slot indices and tag ids, which evaluates over one entity's
facts without allocating.

## Selectors

A selector is an ordered rule list mapping the fact snapshot and request set to
one behavior on a layer: first match by priority, with one concession to
memory, a rule's own stay predicate, which is the only place hysteresis is
authored. A rule's result is a behavior, a nested selector (delegation,
flattened when the rig binds), a named extension point the rig binds a
selector to, or the layer's weight. Weight rules form a second first-match list
over the same inputs: the first whose enter passes weights the layer, and none
passing leaves the rig's constant. They carry no stay, hold or cooldown, so a
weight is a function of the tick's inputs alone. No result names content and no
operand names a rule or behavior; both are absent from the format rather than
rejected.

## Selection

For one layer and one tick: if the winner is latched, its latch is its stay and
only a rule the latch lets interrupt may replace it. Otherwise the winner's
stay is evaluated, and while it passes only higher rules are tried; when it
fails every rule is tried top to bottom. A minimum hold keeps equal or lower
bands out until it expires, and a cooldown keeps a rule out for a while after
it loses. The first rule that passes wins.

`SelectAnimLayer` is the whole of it, pure over its arguments. The system runs
it per entity, and the editor runs it on a copy of the state to explain a tick,
so what the debugger shows is what ran. The system skips an entity whose facts,
requests and content feedback are unchanged, with no hold or cooldown due, no
latch armed, and no selector that reads time or tags.

## Content resolution

After selection, every layer resolves its behavior to content through the
rig's merged slot rows: first match, memoryless, every tick. Whether a new
match applies depends on the behavior's kind. Cyclic and hold content switches
rows mid-behavior, carrying normalized time across. One-shot and flow content
is pinned at entry, and a flow then advances through its sections
(`AnimFlowRunner`). A request superseding the one that drives pinned content
starts it again when it resolves other content, such as a combo's next swing,
and otherwise lets it run on. Content time advances every tick whether or not
selection ran.

A layer with no selector is request-keyed: the request driving it names the
behavior, which is how a door plays `Anim.Door.Open` with no facts and no
selector state. A layer with nothing selected or requested plays its idle
behavior.

The request driving a layer (`AnimLayerDrivingRequest`) is found this way:

- A request-keyed layer plays the newest live request claiming it, ties going
  to the later sequence so every machine picks the same one.
- Failing that, a flow the layer is playing and has not completed keeps the
  cancelled request that drives it while that request is retained, unless its
  behavior aborts on cancel. That is how a flow plays a cancel out.
- A selector layer is driven by the request its latch holds, or by the primary
  record of the one intent its winning rule reads.

## Per-entity state

`AnimContentState` records what each layer plays: the behavior, the slot row
that resolved it, the content, and where in it. Content is an index into the
bound rig's content table, never an asset handle, since the rig's slot maps
hold the clips. A row is named by index and by stable key, so a reload can tell
whether pinned content still exists.

`ContentComplete` is the one value selection reads back. It is published after
content resolution and read by the next tick's selection: the only read
against the dependency order, and never a fact.

## Slot maps and blend overrides

A slot map is one rig's answer to "what plays for this behavior": ordered rows
of a behavior tag, a predicate over facts, and content. Resolution is first
match, memoryless and runs every tick, so content can change under a stable
behavior, as a reload becomes a shotgun reload when the weapon fact says so. A
rig stacks slot maps, a base map then overlays. Rows merge by priority, higher
first, and within a priority by stack order, so an overlay adds or shadows
content without editing the base map's rows.

A blend override is a pairwise exception to the rule that a change blends by
the policy of the behavior it goes to: sprint to slide, fall to land. It names
the behavior a layer leaves and the one it enters and replaces only the blend
policy, never what is selected or what plays. A rig lists override assets in
order, a later one replacing an earlier one's entry for the same pair, and
binds at most `anim.blend.override_cap` pairs: a rig that needs more wants
another behavior or a fact.

## Flows

A flow is a forward-only sequence of sections with one cancel section: the
content a reload, a combo swing or a ledge climb plays. A section plays a clip,
or a slot resolved through the rig's slot map when the section is entered, and
plays once, loops while a predicate holds, or loops a count a request parameter
gives. The cancel section always ends the flow when it finishes.

Control only goes forward. A backward branch fails to compile: a sequence that
needs to go back is making a decision, and decisions belong to gameplay, which
answers a section's lifecycle event with a new request.

`AdvanceAnimFlow` moves one layer through its flow for one tick. A section ends
on the first tick at or past its clip's length and the next begins on that
tick, so every machine agrees on the tick a section changes. At a section's end
the flow goes to the cancel section when cancelling; otherwise it loops,
otherwise takes the first forward branch whose predicate holds, otherwise
moves to the next section, unless the section ends the flow. Past the last
section, or at the end of the cancel section, the flow is complete and holds
its final pose. An immediate cancel goes to the cancel section at once.

`AnimFlowState` remembers only the section, the tick it began, the loop count
and whether the flow is complete. Which flow plays, and the clip its section
plays, are the layer's content. The runner never writes a request: the
authority's anchor and the tail that keeps a cancelled request for a late
joiner are returned for the caller to apply.

## Blendspaces

A blendspace places clips at points along one or two axes, each read from a
fact: walk and run placed by speed, strafes by speed and heading. What plays is
a weighted mix of the samples around the facts' point, all at one normalized
phase, so the mix keeps its footfalls however it is weighted. A slot row plays
a blendspace as it plays a clip or a flow; the heaviest sample's events play.

Weights are gradient-band weights: each sample's weight falls off linearly
toward every other sample and is the least of those falloffs, then all are
normalized. On one axis that is linear interpolation between neighbours; on
two it covers any layout without a triangulation, and a point on a sample
gives that sample alone.

## Events

After content resolution each layer's content time has advanced by one tick,
and every event mark inside the stretch it advanced over is crossed. A mark is
crossed once per content instance (per loop, for cyclic content), measured on
the tick clock rather than the frame clock, so every machine playing the same
content crosses the same marks on the same ticks. A layer whose behavior
changed first leaves the old behavior and enters the new one, firing whichever
lifecycle events they declare; a flow that changed section does the same for
its sections.

Collection and dispatch are separate passes. `CollectAnimEvents` reads content
state and appends fixed-size `AnimPendingEvent` records that name an event by
rig, content and index; it never calls a verb. `DrainAnimEvents` runs on the
owner thread, resolves each record's binding by key in the rig's current
binding set, and offers it through the dispatcher. Every crossing is recorded
in the decision log, fired with its admission, skipped, or below weight, so an
event that did nothing says why.

Scope gates production, not authority. A gameplay event is produced only in a
World with simulation authority, a cosmetic one only where a pose is
presented. Neither makes the invocation authoritative; the verb decides.

## Pose

`EvaluateAnimPose` turns one entity's content into a pose each tick. What each
layer plays becomes a pose, a change to what it plays is absorbed the way the
destination behavior's blend policy says, and the layers compose.

| Blend | Behavior |
| --- | --- |
| Snap | Takes the new pose at once. |
| Crossfade | Keeps the outgoing playback posing beside the incoming one. The incoming weight rises over `in`, the outgoing falls over `out`, and the pose blends by their share. |
| Inertialize | Poses only the incoming content. On the change tick it takes each joint's offset from the incoming pose to what was shown, with the offset's velocity from the tick before, and decays both to rest over `in` along a quintic. A change while one is decaying starts from what was shown, offset included, so stacked changes fold into one offset. |

Each layer blends inside itself before composition, so a change on one layer
never disturbs another. Evaluation is a function of the content state, the
pose state and the tick, and touches only the entity's own pose slot.

Layers compose in declared order into one local pose (`ComposeAnimPose`). The
pose starts at bind, and each layer applies its own pose, already sampled and
blended within the layer, over the joints its mask covers, at its weight:

- **Override** blends each covered joint toward the layer's pose, lerping
  translation and scale and slerping rotation.
- **Additive** adds what the layer's pose changes from its reference pose, its
  content's first frame: the translation difference, the local rotation from
  the reference to the pose, and the scale ratio, each scaled by the weight. A
  layer at its reference adds nothing.

`AnimPoseState` holds how each layer absorbs changes: the playback it was last
posed from, a crossfade's outgoing playback, and when the current blend began.
Only a World that presents a pose carries it. The per-joint half
(inertialization offsets, each layer's pose, and the composed pose of this tick
and the one before, which render extraction interpolates between) is variable
in size and lives in the World's `AnimPosePool`. `AnimPoseSystem` poses every
rigged entity once per fixed tick, after movement. On the owner thread it
assigns and shapes pool slots and resolves each entity's binding, the only work
that touches shared state; it then evaluates entities across the job system,
each touching only its own components and slot, or inline in entity order when
there are no workers. The inline order is the reference the parallel path must
match.

## Root motion

A root curve is data, and content time is a function of a request's start
tick, so the motion between two content times is the same on every machine and
every replay of the same ticks. Movement applies it; animation moves nothing.

An entity is carried while its base layer plays a behavior flagged
`root_motion` whose clip has a root curve. The motion on a tick is the curve
between that tick's content time and the previous tick's, turned into the
character's facing. On a request-keyed base layer, what played on a tick
follows from the request records alone, each carrying its start and cancel
ticks, so a replayed tick is carried as the authority carried it, through a
cancel or a corrected start. A selector's base layer is carried by what it
plays now: selection is state, and a replay across a selection change is
carried by the newer choice. While carried, the character goes nowhere else
across the ground even when the curve is still, so a mantle's first frames
hold it in place.

## Timing across machines

What plays is the same on every machine; when it started must be too. Content
time runs from:

1. the start tick of the request driving it, which a late joiner also sees;
2. the cancel tick of a request whose content was cut short by that cancel,
   so a machine that hears of the cancel late still switches in step;
3. otherwise, the tick the content changed on this machine.

The third case is local. A looping behavior that no request drives, such as an
idle, has a phase that differs on a machine that joined late or had a
prediction refused: it plays the same behavior and content, but its own phase.
That is by design. Only requests carry a start tick to reconstruct from, which
is why a behavior may reconstruct on late join only when requests alone reach
it.

## Replication

`AnimRequestSet` is the only animation component that replicates. A request
names an entity and gameplay tags, both local numbering, so the set travels as
an image in names every machine shares: a `NetEntityId` for the source and a
tag's wire key for the intent, the source tag and any param the record marks as
a tag. Ticks travel as they are, since they are already the authority's, and so
does the rig timing identity, which is built from names.

A record is two runs, its ticks and its words, so a change to one record resends
that record and the set's sequence and nothing else. An empty slot is all
zeros. A source this machine was never sent arrives as no entity and an intent
this build does not register as no tag; the record stays, still ordered and
retained, and matches no rule.

A client predicts only issuing, through `AnimRequestJournal`. On the authority,
or with no session, a request is simply issued; on a client it is issued as a
prediction and remembered with the command whose processing issues it for
real. A snapshot replaces the set with the authority's, wiping every
prediction. After each one, `Reconcile` forgets what the authority has decided
(every prediction whose command it processed, whether it kept or refused the
request), removes that prediction from the set if the snapshot did not, and
issues what is still undecided again on top. Nothing in animation is rewound:
content follows the set it is given, and a corrected start restarts content
where the authority put it. Cancels, anchors and tails are the authority's.

`AnimRigTimingIdentity` hashes what two machines must agree on about a rig for
one request set to play out the same on both: the fact layout and its
derivations, the request schema, which rule wins, what each behavior latches
and how it cancels, which content a row plays and for how long, a flow's
sections, loops and branches, and every gameplay-scope event. It is built from
tag, slot and intent names. Blends, weights, masks and cosmetic events are
left out: they change how a pose looks, not what happens, so a reload touching
only those stays compatible with a session in progress. The authority stamps
its identity on the request set; a machine whose binding differs records
`TimingDisagreed` and cannot trust reconstruction until the rig reloads or the
session rejoins.

## Measurements

Recorded with `scripts/bench_animation.sh`: the profile preset (release code
generation with symbols), pinned to the performance cores of an Intel i7-13620H,
each figure a median after warm-up.

| Measurement | Result |
| --- | --- |
| Binding a rig: prop, two-layer, character | 0.8 µs, 7.3 µs, 11.6 µs |
| Headless tick over 64 cosmetic props: skipped, run | 0.016 ms, 0.030 ms |
| Headless tick over 1024 cosmetic props: skipped, run | 0.27 ms, 0.49 ms |
| Pose pass, 64 two-layer 62-joint characters: 0, 3, 7 workers | 0.46 ms, 0.20 ms, 0.12 ms |
| Pose pass, 256 characters: 0, 3, 7 workers | 1.92 ms, 0.57 ms, 0.40 ms |
| Editor preview, replay from tick 0 to 600 and to 3600 | 0.96 ms, 5.46 ms |
| Editor scenario batch over the fixture project's 8 scenarios | 9.3 ms |

A steady tick allocates nothing: `AnimSteadyStateAllocation` runs 600 ticks of
facts, selection, content, events and poses over two rigs, with speed changes,
landings and requests issued and cancelled, after the same input has reached
every state once, and counts every form of allocation, serially and with three
workers.

On the wire (`AnimRequestBandwidth`), each case acknowledges its baseline and
counts the next snapshot's bytes above one that describes nothing, which is the
23-byte snapshot header. An animated entity at rest, and a request that keeps
holding, cost nothing. Issuing a held request costs 90 bytes and ending it 86:
the entity's envelope plus the changed record. Filling all eight records at
once costs 650.

Every animation, data-asset, document-layer and gameplay-tag suite (412 tests
across core, framework, runtime and editor) passes under the `asan` preset,
with leak detection off, and the `tsan` preset. Each instrument was first shown
to fire on a deliberate out-of-bounds read and data race. An `asan` build needs
`ASAN_OPTIONS=detect_leaks=0` because component code generation runs under it.

## Diagnostics

`AnimDecisionLog` is a fixed 64-record ring on the entity. It records why
selection, content, requests, blends and events changed. A deeper or
dev-default log is recorded in `docs/deferred.md`.

Every state change the runtime makes writes one record with a cause, and a
change with no cause is a bug: the runtime's invariants are enforceable only
because every change is attributable. An entity carries a log only when it opts
in, and every writer takes a nullable log. Records hold compact ids, which an
inspector resolves against what is still loaded.

The console:

- `anim.trace` lists animated entities as `index:generation`, and
  `anim.trace <entity>` starts recording one.
- `anim.trace.export <entity> <file>` writes an `animation.trace` document:
  every record with names resolved, how many the ring had overwritten, and
  `"captured": "decisions"` — a trace has no pose history. The animation
  editor reads one back in Problems and changes > Imported trace.
- `anim.risk` reports each bound rig's content risk and what its entities in
  this World carry and have left unplayed.
- `anim.blend.override_cap` limits pairwise blend overrides per rig.

Content risk (`MeasureAnimRigRisk`) counts blend overrides, selector rules, the
largest selector, and flows longer than eight sections. It flags intents
nothing plays and behaviors that both a fact and a request select. Each is a
sign a rig is drifting toward a graph; none is an error.

At runtime, `AnimContentState::UnplayedRequests` counts requests that ended
without any layer playing them. A request is played when a layer's content was
driven by it, or the rule a layer is running reads its intent.
`ReportAnimWorld` sums it per rig with each entity's footprint.
