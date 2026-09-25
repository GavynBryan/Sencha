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
its speed and start; the old clip player is gone, and the animation editor's
Migration panel converts scenes that still name it.

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

## Diagnostics

`AnimDecisionLog` is a fixed 64-record ring on the entity. It records why
selection, content, requests, blends and events changed. A deeper or
dev-default log is recorded in `docs/deferred.md`.

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
