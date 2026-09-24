# Animation Editor

The animation workspace is being built alongside the data-driven runtime. Its
current surface auditions cooked skinned meshes and clips, simulates rigs under
scenarios, edits request schemas and selectors, authors clip events, edits flow
structure and bone masks, and previews blends: the viewport shows the runtime's
own pose pass, with blend inspection, blendspaces and a blend A/B recorder.

## Content audition

Launch `animation_editor --project path/to/project.senchaproj`. Optional
`--mesh asset://...skmesh` and `--clip asset://...sanim` arguments select initial
content. Kettle also exposes an Animation action for each project.

For the repository's two-joint fixture:

```text
animation_editor --project test/fixtures/content/animation_preview.senchaproj --mesh asset://meshes/dev/golden_rig.skmesh --clip asset://meshes/dev/golden_rig.sanim
```

The left pane selects meshes, skeletons, clips, and a material override. A mesh
selects its own skeleton. Selecting a skeleton directly removes the mesh; the
hierarchy remains inspectable. Incompatible clips are rejected without replacing
the previous valid selection. The viewport uses the runtime sampler, palette
builder, compute skinning pass, and forward material pass. Drag to orbit, scroll
to zoom, and use Frame mesh to restore framing.

The bottom timeline offers play/pause, restart, previous/next fixed tick, speed,
looping, and normalized-time inspection. Playback uses 60 Hz ticks. Clip endings
fall on the first tick at or beyond their duration; a non-looping clip samples its
exact final pose. Inspection pauses playback and samples a scratch pose without
changing the playback tick. Return to playback tick restores that pose; Play
continues from that tick. No audition operation emits an authored verb, modifies
gameplay state, or dirties a content asset.

The right pane reports the selected dependency paths, skeleton joints, and load
errors. Material selection applies one preview override across mesh sections.
Asset values are captured on selection; skeletal-content hot reload is not yet
implemented. Refresh asset list enumerates the mounted registry, not the disk.

## Request schema authoring

Open a request schema from the content browser to edit it alongside the animated
preview. The fixture project includes `asset://animation/requests.sdata`.
`animation.request_schema` is registered with the existing structured-data asset
pipeline, so new schemas can also be created in Data Editor. Both editors use the
same document transactions and validation implementation in editor-common.

The request pane edits intent tags and up to four named parameters of kind float,
int, bool, or tag. Duplicates, invalid value kinds, and capacity violations report
their precise field paths. Names remain text in the asset; no runtime tag IDs are
persisted. Unknown fields are retained and diagnosed, not silently discarded.

Text edits coalesce into one undo operation. Escape, focus loss, and document
switching cancel an unfinished edit. Undo/redo and Save are available through the
shell and pane. Open documents retain independent history; selecting one does not
change the preview subject or time. Reload is explicit and refuses dirty documents;
Save refuses externally modified files. Closing with unsaved changes offers Save
all, Discard, or Keep editing.

This pane edits the request contract only; issuing and cancelling requests against
a rig happens in the simulation panels below. Preview clip selections remain
transient and do not dirty documents.

## Rig simulation under a scenario

Select an `animation.rig` in **Rig and scenario**. The rig is simulated in an
isolated preview World at the scenario's fixed tick rate, using the runtime's
own binding, fact gathering, derivations and request lifecycle. The editor
replaces only gameplay: each gathered fact reads a scenario-owned input, and
named scenario participants issue requests through the normal request API.

- **Facts** lists every slot of the bound layout. Gathered facts take typed
  inputs (bool, float, int, tag by name); derived facts are read-only and show
  their derivation, and the panel says whether derived facts are exact or still
  warming up toward the rig's horizon. While paused, the Next tick column is a
  disposable evaluation of the edits scheduled for the next tick. Select a fact
  to plot its history.
- **Requests** issues from a form derived from the rig's request schema (source,
  intent, lifetime, layers, typed parameters) and lists the live records with
  age, primary-record status, cancel reason and retained tail. Capacity refusals,
  supersession and impulse deduplication are reported per tick.
- **Simulation** plays, pauses, steps and restarts. Speed changes how quickly
  fixed ticks are scheduled, never their length. Running to an earlier tick
  replays the scenario from tick 0.
- **Problems and changes** shows the rig's bind diagnostics and the scenario's
  located problems, the entity's decision history, and what is unsaved.

Every live edit is recorded into the working scenario on the next tick, so the
live session and a replay of the saved scenario run the same code on the same
ticks. Acting at a tick the scenario has already scripted past drops the later
actions (a new branch). **Save scenario** writes an editor-only sidecar beside
the rig's source, `<rig>.sanimscenario`; the asset scanner does not register it,
and nothing in the preview writes the rig or its schemas. Opening a rig restores
its sidecar when one exists.

The editor runs without a game module, so names a module would declare are not
known here. A scenario may list `declared_tags` as preview fixtures; they are
registered only in the preview World and shown as fixtures. Nothing a scenario
names is registered on its behalf: an unknown fact, intent, parameter or tag is
a located problem.

The fixture project ships `asset://animation/hero.rig.sdata` with a
landing-and-reload scenario beside it:

```text
animation_editor --project test/fixtures/content/animation_preview.senchaproj
```

## Selection: rules, behaviors, slot maps, decisions

With a rig open, each simulated tick runs the runtime's selection and content
resolution and records why every rule did or did not win.

- **Rules** lists the layer's flattened selector in evaluation order: band,
  enter and stay (read from where each row was authored, however deeply it was
  delegated), behavior, and this tick's verdict -- winner (entered or stayed),
  a failed row with the value it read, cooldown, blocked by latch, blocked by
  hold, or not reached. While paused, *Show next tick* explains the next tick
  from disposable copies, so an edited fact or rule shows its effect before
  time moves. With a selector document active, the same panel edits it: rules,
  priority, behavior, hold and cooldown, and a typed predicate builder whose
  rows pick facts, requests and parameters from the bound rig.
- **Behavior** shows the selected behavior's policy (kind, blend, latch and
  what may interrupt it, late join), the rules that select it and the rows that
  resolve it.
- **Slot map** shows the rig's merged rows -- priority, then stack order -- with
  each row's source and which layer is playing it now. *Audition this clip*
  switches the viewport to the clip without touching the simulation; the
  viewport's Simulation/Audition switch goes back.
- **Decisions** shows the live tick or, with Live unchecked, any recorded tick:
  each layer's winner, latch, content and time, every rule's verdict, and the
  decision records written on that tick. Inspecting history never changes the
  live session.

Picking a rule, behavior, row or clip in any of these moves one shared
navigation state and nothing else. Valid edits to any open animation document
reach the running preview as soon as they are committed (and on undo or redo);
an invalid working edit leaves the preview on the last valid version and says
so. Only Save writes the file.

The fixture rig now carries a locomotion selector with a stay-hysteresis sprint,
a latched landing and a request-latched reload. Its content is the fixture's one
cooked clip on every row, so the rules and decisions change visibly while the
pose does not; `AnimationSelectionEditingTests.cpp` covers content switching
headlessly.

Not yet: tag-set (tag container) inputs, selectors and content resolution,
recorded-tick inspection without replay, and loading a project's module
vocabulary. `AnimationPreviewSessionTests.cpp` covers the session headlessly:
next-tick application, derived history, capacity/primary/retention, exact
replay of a saved take, branching, unknown names, and asset immutability.

## Clip events

A clip cooked from a mesh source (`asset://<source>#anim:<clip>`) carries
events: timeline marks that invoke authored bindings. They are authored in the
source's import sidecar, `<source>.meta`, because the cooked clip is rebuilt
from the source on every import; the cook copies them into `.sanim`.

- **Clip events** picks a clip the open rig plays (or the auditioned one) and
  shows its track: circles are cosmetic events, diamonds gameplay events, and
  the playhead follows whichever preview is playing the clip. Double-click to
  add, drag to move, right-click to delete. A drag is one undo step; Escape,
  focus loss or hiding the panel cancel it and put the marker back.
- The inspector edits name, normalized time, scope and a cosmetic event's own
  weight threshold, then the binding: search the rig's bindings, open the
  file that declares one, or create one from any declared verb (each argument
  becomes an input of its name, for you to narrow). Each input the binding
  takes gets a field typed by the argument it fills, lists every argument it
  fills, and is checked against all of them with the rig binding's own
  conversion. The verb's contract is shown beside it.
- Valid working events reach the preview at once through the production rig
  binding; invalid ones leave it on the last valid events, and say why. Only
  Save writes the sidecar, and it refuses a sidecar changed on disk.
- **Event admissions** shows every crossing and lifecycle event the simulation
  produced, with what its binding answered: accepted, unavailable, unresolved,
  invalid arguments, stale, or refused past capacity, and suppressed or
  skipped crossings that were never offered. The preview runs no game: a verb
  answers Unavailable unless a recorder stands behind it, and a recorder's
  accepted invocations are listed as a recorder's, with their argument values.
  The preview's role (authority or client) decides whether gameplay events
  are produced at all. Role and recorders are scenario state, saved with it;
  changing either replays to the current tick.
- A rig names the `authored.bindings` files its events draw from in its
  `bindings` list. Bindings resolve by key when the rig binds; nothing holds a
  verb id or a compiled binding across a reload, so an edited, reordered or
  removed binding takes effect on the next tick.
- Behaviors may declare `on_entered` and `on_exited` bindings, handed the
  behavior's tag as their `behavior` input. The Behavior panel shows them and
  whether they resolved; they are edited in the behavior set (Data Editor).

When the project has a game module, the editor loads it for its vocabulary hook
only -- the verbs and tags the project declares reach each preview World -- and
never starts it.

`AnimationEventPreviewTests.cpp`, `AnimationEventEditingTests.cpp` and
`AnimationClipEventsDocumentTests.cpp` cover these headlessly: admissions with
and without recorders, role, replay, audition isolation, binding removal and
restore, marker placement through admission, drag cancel, binding creation, and
per-destination input checks.

## Flows, layers and masks

A slot row may play a flow (`animation.flow`) instead of a clip: forward-only
sections, each a clip or a slot resolved when the section is entered, that loop
while a predicate holds or as many times as a request parameter says, branch to
a later section, and go to one cancel section at once or at the section's end.

- **Flow** draws the flow the selected layer plays as a strip of sections sized
  by length, with the playing section lit, its progress, the loop pass, and the
  cancel section in a lane beneath, and states how each section is left. With
  the flow's document open (Edit, or Flows in the content browser) it edits the
  structure: loop kind, whether a section ends the flow, whether a cancel
  leaves it at once, branches, the cancel section, and sections added, moved
  and removed. A branch can only be added to a later section, and a move that
  would turn one backward is refused, so control never goes back. Loop and
  branch conditions show as text and are edited in Data Editor.
- **Layers** lists the rig's layers with mode, current weight (hover for the
  weight rule that set it, or the rig's constant), mask size and what each
  plays. Mute and solo change only what the viewport shows; the simulation, its
  events and the rig are untouched. The viewport composes every shown layer:
  override layers blend toward their pose over their mask, additive layers add
  what their clip changed from its first frame, each at its weight.
- **Skeleton and masks** shows the rig's skeleton as a tree with each layer's
  coverage beside every joint (`#` covers, `.` does not), dimming joints the
  selected layer leaves out. Right-click a joint to add it, or remove it, with
  or without everything below; each is one undo step on the rig document. The
  mask's steps are listed in order with their own remove buttons. The first
  layer is always unmasked. Joints are named as the skeleton names them, so an
  unknown or ambiguous joint is a located problem.

Loop and branch conditions edit in the Flow pane with the same predicate
builder the rule table uses, and a count loop's intent and parameter are chosen
from the rig's request schema. In the viewport, **Joints** draws the skeleton on
screen: click a joint to select it (the skeleton tree follows), and right-click
one for the same mask steps the tree offers.

A selector rule can result in a layer weight, a constant or a float fact,
instead of a behavior; the first weight rule to pass weights its layer.

The fixture project carries the six flow shapes the architecture was checked
against, under `asset://animation/examples/`, each a rig with a runnable
scenario: pump reload (inserts until fire cancels at once), melee combo
(advanced by a superseding request), charge attack (hold while held, release
variant by parameter), draw and fire (masked upper layer over walking), ledge
climb (a failed climb drops through the cancel section) and door (a fixed
request swings it; a fact rests it). `AnimationFlowExamplesTests.cpp` runs each
through the asset pipeline and checks its sections and ticks;
`AnimationLayerEditingTests.cpp` and `AnimationLayerDisplayTests.cpp` cover the
mask and flow edits and the composed layers headlessly.

Not yet: section events on the event timeline.

## Blends and blendspaces

The viewport shows the pose the runtime's pose pass made for the simulated
subject, not a preview composition: the same pass a game runs after movement.
A change to what a layer plays blends by the destination behavior's policy, or
by a pairwise override in an `animation.blend_overrides` asset the rig lists:
snap, crossfade (the outgoing playback stays alive beside the incoming one), or
inertialize (the default: each joint's offset and velocity from what was shown
decays to rest along a quintic, and a change mid-decay folds into it). A
behavior whose blend carries phase starts where the one it replaces had got to
in its sync group.

- **Blends** shows each layer's blend in progress -- a crossfade's source and
  progress, an inertialization's progress and remaining offset -- the recent
  blend records (mode, duration, the offset it began from, whether an override
  chose it) and the bound overrides against `anim.blend.override_cap`.
- **A/B** in the same panel: *Record A* takes the simulation's pose on every
  kept tick with the scenario as written. Edit a blend, then *Replay B against
  A*: the scenario runs again from tick 0 with the same inputs, clock, seed and
  start pose, and the two compare tick by tick -- the largest joint residual
  plotted, and where the worst fell. A is drawn as an orange ghost. Takes of
  different scenarios are refused rather than compared.
- **Blendspace** draws the mix the selected layer plays: samples on their axes
  sized by weight, the point the facts put it at, its phase, and which sample's
  events play. Drag the point to set the axis facts for the next tick, as a
  recorded live edit.
- Inspecting a recorded tick (Decisions, Live unchecked) shows the pose the
  pass made then.

The fixture examples include `moving`: a blendspace on Speed entered with
carried phase and an inertialized change, and an override that crossfades back
to idle.

Not yet: a blendspace or blend override pane (they edit in Data Editor), and
per-layer ghosts.

## Ownership

`animation_authoring` is a GUI-independent library. `AnimationClipPreviewSession`
owns audition time and pose scratch. `AnimationPreviewSession` owns the preview
World, fixed clock, scenario runner and tick history; `AnimationScenario` is the
sidecar format. `AnimationPreviewWorkspace` owns asset leases
and selection, then extracts `AnimationPreviewScene`. The rendering feature
consumes that scene, never a simulation World. The application removes its render
features before destroying the asset stack. No game module is activated during
content audition.

Playback tests in `test/editor/AnimationClipPreviewSessionTests.cpp` exercise the
production sampler without graphics. `AnimRequestSchemaTests.cpp` covers the
schema compiler, diagnostics and document transactions; `AnimRequestSchemaAssetTests.cpp`
covers loading through the headless runtime asset composition.

## Remaining paired runtime/editor stages

The runtime these build is specified by
[docs/plans/animation-runtime.md](../../docs/plans/animation-runtime.md), and
the staging by [docs/plans/animation-authoring.md](../../docs/plans/animation-authoring.md).
These are required implementation work, not capabilities of the current editor:

1. Remaining from the facts/requests stage: rig and fact-schema panes in this
   editor (they are authored in Data Editor today), a command that creates a
   rig with its behavior set, slot map and scenario, adding and removing a
   scenario's fixture tags here, tag-set inputs, and mounting the engine's own
   content so a rig can extend the engine fact schema in this editor. Root
   metadata lands with its consumer in the root-motion stage.
2. Remaining from the selection stage: a behavior-set and slot-map pane (they
   edit in Data Editor today, and valid edits there still need a reload here),
   and the predicate text form.
3. Remaining from the events stage: lifecycle bindings edited in their own
   inspector rather than Data Editor, and event marks drawn on the shared
   timeline beside sections and requests.
4. Remaining from the blending stage: blendspace and blend override panes, and
   fading a layer's weight over time rather than stepping it.
5. Request replication and gameplay-owned request reconciliation, with remote
   fact snapshots and paired late-join/correction previews. Do not rewind
   presentation animation with movement replay.
6. Extracted root curves and replayable movement-owned motion sources, with
   translation/yaw plots and requested/composed/achieved displacement inspection.
7. Compatible single-clip migration, presets, hot-reload remapping, complete
   decision-history capture/import, full cross-asset undo/redo, and workflow tests.

Selection remains facts + requests -> selectors -> behaviors -> slot maps ->
content. There is no animation transition graph. Shipping gameplay receives
animation output only through the authored API; editor inspection is not a new
gameplay dependency.
