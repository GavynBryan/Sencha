# Animation Editor

The animation workspace is being built alongside the data-driven runtime. Its
current surface auditions cooked skinned meshes and clips; it is not yet the
selector, flow, or event-track authoring environment.

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

## Ownership

`animation_authoring` is a GUI-independent library. `AnimationClipPreviewSession`
owns audition time and pose scratch. `AnimationPreviewWorkspace` owns asset leases
and selection, then extracts `AnimationPreviewScene`. The rendering feature
consumes that scene, never a simulation World. The application removes its render
features before destroying the asset stack. No game module is activated during
content audition.

The playback tests live in `test/editor/AnimationClipPreviewSessionTests.cpp`.
They exercise the production clip sampler without a window or graphics device.

## Remaining paired runtime/editor stages

These are required implementation work, not capabilities of the current editor:

1. Fact/request schemas, fixed-capacity requests and bounded fact history, with
   editable preview snapshots, request controls, saved scenarios, shared document
   transactions, and actionable schema diagnostics.
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
