# Animation Editor

The animation workspace is being built alongside the data-driven runtime. Its
current surface auditions cooked skinned meshes and clips and edits request
schemas; it is not yet the selector, flow, or event-track authoring environment.

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

Not yet: tag-set (tag container) inputs, selectors and content resolution,
recorded-tick inspection without replay, and loading a project's module
vocabulary. `AnimationPreviewSessionTests.cpp` covers the session headlessly:
next-tick application, derived history, capacity/primary/retention, exact
replay of a saved take, branching, unknown names, and asset immutability.

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
   editor (they are authored in Data Editor today), tag-set inputs, the
   project's module vocabulary in the preview World, skeletal joint identity,
   and clip event/root metadata formats (landing with their consumers in
   stages 3, 4 and 7).
2. Flat selectors, behaviors and slot overlays, with live winner/failure views,
   typed predicates, cross-asset navigation and decision history. Preserve the
   selector-free Prop and one-layer Simple paths.
3. Timeline marks using authored binding keys and typed inputs, with the bounded
   owner-thread invocation drain, explicit preview authority, recorder bindings,
   suppression/skipping diagnostics, and binding revision refresh.
4. Forward-only flows, latches and layers, with section/cancel timelines,
   practical bone masks, pinning diagnostics and shared request anchors.
5. Blendspaces, inertialization, crossfade and phase carry, with repeatable
   transition A/B scenarios and override budgets.
6. Request replication and gameplay-owned request reconciliation, with remote
   fact snapshots and paired late-join/correction previews. Do not rewind
   presentation animation with movement replay.
7. Extracted root curves and replayable movement-owned motion sources, with
   translation/yaw plots and requested/composed/achieved displacement inspection.
8. Compatible single-clip migration, presets, hot-reload remapping, complete
   decision-history capture/import, full cross-asset undo/redo, and workflow tests.

Selection remains facts + requests -> selectors -> behaviors -> slot maps ->
content. There is no animation transition graph. Shipping gameplay receives
animation output only through the authored API; editor inspection is not a new
gameplay dependency.
