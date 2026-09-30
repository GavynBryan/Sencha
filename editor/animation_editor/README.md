# Animation Editor

The animation workspace is being built alongside the data-driven runtime. Its
current surface auditions cooked skinned meshes and clips, simulates rigs under
scenarios, edits request schemas and selectors, authors clip events, edits flow
structure and bone masks, and previews blends: the viewport shows the runtime's
own pose pass, with blend inspection, blendspaces and a blend A/B recorder.

## Content audition

Launch `animation_editor --project path/to/project.senchaproj`. Optional
`--mesh asset://...skmesh` and `--clip asset://...sanim` arguments select initial
content.

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

Text edits coalesce into one undo operation. Escape and focus loss cancel an
unfinished edit; switching documents commits it. Undo/redo and Save are available through the
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
actions (a new branch); further edits made while paused on that tick join the
same branch. **Save scenario** writes an editor-only sidecar beside
the rig's source, `<rig>.sanimscenario`; the asset scanner does not register it,
and nothing in the preview writes the rig or its schemas. Opening a rig restores
its sidecar when one exists.

A preview World knows the names the game will: the game module's own, when its
vocabulary hook is loaded, and every name the project's
`gameplay.tag_declarations` assets list, which the runtime registers when
content loads. A scenario may also list `declared_tags` as preview fixtures for
names a module would declare when none is loaded; they are registered only in
the preview World, and **Rig and scenario** adds and removes them (the session
replays to the current tick under the new names). Nothing a scenario names is
registered on its behalf: an unknown fact, intent, parameter or tag is a
located problem.

**New rig** in the same panel starts a rig from a name, a tier and the clips it
plays, ticked in order. It writes `animation/<name>/` into the project's first
content root -- a behavior set, a slot map, a request schema, selectors for the
selector tiers, the rig, a tag declaration listing every name the rig uses, and
its scenario -- and opens it ready to play. The tiers:

- *Prop*: the first clip idles, each other clip plays while a request of its
  name (`Anim.<Clip>`) is held. Doors, machinery.
- *Simple*: rules over the engine's facts. The first clip idles, the second
  plays while `Speed` is above 0.1, and the rest are actions a request plays
  through once. Most enemies.
- *Character*: Simple's idle and locomotion, and an upper-body layer masked
  from a chosen joint that plays the actions over them, shown only while one is
  requested.

The skeleton is the clips' own; clips of different skeletons cannot share a
rig. It never overwrites.

The engine's own content root is mounted after the project's, as at runtime,
so a rig may extend `asset://animation/engine.facts.sdata`.

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

The fixture rig carries a locomotion selector with a stay-hysteresis sprint,
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
from the source on every import; the cook copies them into `.sanim`. A clip's
events document reads the whole sidecar, edits that clip's entry, and writes
every other clip's entry back as it found it.

- **Clip events** picks a clip the open rig plays (or the auditioned one) and
  shows its track: circles are cosmetic events, diamonds gameplay events, and
  the playhead follows whichever preview is playing the clip. Double-click to
  add, drag to move, right-click to delete. A drag is one undo step, and the
  preview plays the marker's new time once it is released; Escape, focus loss
  or hiding the panel cancel it and put the marker back.
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

## Two machines: the session lab

**Session lab** runs the working scenario on an authority, and on a client
that gets the authority's requests only as snapshot bytes. Those bytes go
through the production change store, snapshot writer and applier, request codec
and prediction journal, across a link with a join tick, a latency in ticks and a
loss rate. The loss rate drops a fixed pattern of snapshots, so a run repeats.
The status line at the top always says which machine is which, and that facts
are synthetic scenario inputs on both.

Both machines read the same scenario facts on the same ticks, standing in for
replicated gameplay; only requests cross the link, and the client hears nothing
before its join tick. The client shares the authority's clock: latency is
modelled by when snapshots are delivered, not by a clock offset, and
acknowledgements return at once.

- **Client guesses** inject a request the client predicts on a tick, plus the
  tick the authority decides the command behind it, confirming or refusing it.
- The strip marks each tick green where the client plays what the authority
  does (behavior, content, time, flow section), red where it does not, and grey
  before the client joined. A notch marks each lost snapshot. Below it is the
  client's pose residual against the authority's.
- **What the joiner received** lists the requests of the client's first
  snapshot with their anchors and tails. Clicking a tick sets the two machines'
  layers, sections, requests (guesses marked) and decisions side by side.

`AnimationSessionLab` is the headless model; `test/editor/AnimationSessionLabTests.cpp`
covers a late join over a lossy link on the `pump_reload` example, a refused
guess, and repeatability.

Not yet: the client's pose drawn as a ghost in the viewport.

## Root motion

A clip carries the character when its source's sidecar asks for root motion
and a behavior flagged `root_motion` plays it on the base layer. A scenario
with a `movement` block puts the character on a floor and runs the game's
movement pipeline and mover after content resolves, so the clip carries it
and a wall can stop it.

- **Root motion** toggles *Move the character*, and edits the walls: add one
  ahead, set its centre and half size, or remove it. Each change replays the
  scenario to the current tick. It plots the playing clip's root curves --
  right, back and turn over the clip -- with where the base layer is, and
  totals the distance carried against the distance achieved, naming the ticks
  a wall cut short.
- **Path** in the viewport draws the character's path on the floor, each move
  a collision cut short (red, where it would have gone), and the walls. The
  character is drawn where it stands.

`test/editor/AnimationRootMotionPreviewTests.cpp` covers a carried character, a
wall stopping it, and movement as scenario state.

## Documents, undo and saving

The **Document** panel shows each open document as a tab, drawn through the
same tabs and schema-generated form Data Editor uses
(`editor/common/src/ui/DataDocumentTabs.h`); closing a tab with unsaved changes
asks Save, Discard or Cancel. Every field of a rig, behavior set, slot map,
selector, flow, blendspace, blend overrides, fact or request schema, bindings
file and tag declaration is editable here; the purpose-built panels above are
the richer views of the same documents. **New asset** creates any of those
types, and a reference field's Pick names it.

A name content introduces as a gameplay tag -- a new behavior, intent or
layer -- fails the rig's binding until something declares it. The Problems tab
lists those names and **Declare them beside the rig** adds them to the rig's
tag declarations as one undo step there; working declarations reach the
preview before they are saved.

Undo and redo take the newest step across every open document and clip's
events, whichever the author is looking at, and bring its document forward.
Any interaction still open is cancelled first; switching documents commits it.
Only a committed version reaches the preview, never an edit in progress. An
edit to an asset the preview has not loaded yet -- a selector edited before
the rig names it -- reaches the preview once something loads it.

**Save all** (Changes tab, the File menu and the exit prompt) saves every
changed document it can and holds back each whose file changed on disk since
it was read. Each held-back file is settled with **Keep mine**, which writes
the working version over it, or **Take the file's**, which adopts the file's
version as an undo step. Keeping a clip's events re-reads the sidecar first,
so another clip's change there survives. Closing the editor with unsaved
documents asks first.

The journal, save report, document tabs and exit prompt are the shared document
layer in `editor/common` (`documents/`, `data/DataDocumentSet.h`), the same one
Data Editor uses; clip events join it as `AnimationClipEventsSet`.

## Qualification

- **Scenario batch** runs every saved scenario in the project -- each under its
  own rig, or all under the open rig -- twice from tick 0, in a session of its
  own. A scenario fails on an error or a second run that differs from the
  first, and warns on a warning or a request no layer played.
- **Migration** finds scenes that still name the retired clip player and turns
  each into a one-layer rig that plays the clip as the player did.
- **Content risk** (Problems and changes) shows the open rig's risk measures,
  the previewed entity's footprint and its unplayed requests -- what `anim.risk`
  reports in a game.
- **Imported trace** reads a trace written by `anim.trace.export` in a running
  game: every decision record as logged, and what the trace did not capture.

The runtime side of these is described in
[docs/gameplay/animation.md](../../docs/gameplay/animation.md).

## Ownership

`animation_authoring` is a GUI-independent library. `AnimationPreviewWorkspace`
is its composition root: it holds the parts below, in destruction-safe order,
and runs only the operations that span them (opening or creating a rig,
migrating clip players, the scenario batch, advancing the clocks, extracting
the viewport).

| Part | Owns |
| --- | --- |
| `AnimationContentTags` | Declared tag names, unsaved declarations included, and the vocabulary hook. |
| `DocumentSourceSet`, `DataDocumentSet`, `AnimationClipEventsSet` | Open documents, the one journal, saves and conflicts. |
| `AnimationContentLists` | The project's assets by kind and data subtype. |
| `AnimationAuditionSelection` | The auditioned mesh, skeleton, clip and material, their leases and `AnimationClipPreviewSession`. |
| `AnimationViewportExtraction` | The `AnimationPreviewScene`, display modes and ghost; it reads clocks and never advances them. |
| `AnimationRigScenario` | The rig under simulation: `AnimationPreviewSession` (World, fixed clock, history), its scenario sidecar and navigation. |
| `AnimationTakeComparison`, `AnimationLabRun`, `AnimationClipPlayerScan` | Take A and its replay, the session lab, and scenes still naming the clip player. |

Document edits that touch the rig (`AnimationRigDocumentEdits.h`) are free
functions over the document set. A panel takes the parts it uses; one that
spans many parts or runs a cross-part operation takes the workspace. The
rendering feature consumes the extracted scene, never a simulation World. The
application removes its render features before destroying the asset stack. No
game module is activated during content audition.

Playback tests in `test/editor/AnimationClipPreviewSessionTests.cpp` exercise the
production sampler without graphics. `AnimRequestSchemaTests.cpp` covers the
schema compiler, diagnostics and document transactions; `AnimRequestSchemaAssetTests.cpp`
covers loading through the headless runtime asset composition.

## Remaining paired runtime/editor stages

The runtime these build is specified by
[docs/plans/animation-runtime.md](../../docs/plans/animation-runtime.md), and
the staging by [docs/plans/animation-authoring.md](../../docs/plans/animation-authoring.md).
These are required implementation work, not capabilities of the current editor:

1. Tag-set scenario inputs and the predicate text form.
2. Purpose-built panes beyond the Document panel's form: a behavior policy
   inspector linked to every rule and row that reaches it, a slot map view of
   overlay precedence, a blendspace layout editor, and fact derivations with
   their dependencies. Lifecycle bindings edited in their owning inspector.
3. Event marks drawn on the shared timeline beside sections and requests.
4. Fading a layer's weight over time rather than stepping it.
5. The take-A ghost drawn where the character stood, and a composed-motion
   view beside requested and achieved.
6. Picking up edits made to open documents by another editor, before save.

Selection remains facts + requests -> selectors -> behaviors -> slot maps ->
content. There is no animation transition graph. Shipping gameplay receives
animation output only through the authored API; editor inspection is not a new
gameplay dependency.
