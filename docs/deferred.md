# Deferred work

Work deliberately left for later. Each entry names what was deferred, where the
code lives, and the concrete trigger that makes it due. When a trigger lands,
do the work and delete the entry.

## Animation decision log depth

- **What:** the runtime plan (`docs/plans/animation-runtime.md`, decision
  history) asks for a global cvar setting the decision log's ring depth.
- **Where:** `AnimDecisionLog` in `engine/include/anim/AnimDecisionLog.h` is a
  component with a fixed 64-record ring (`kAnimDecisionLogCapacity`). A
  runtime depth needs the records out of relocatable component storage, into a
  registry-owned ring addressed from the component.
- **Trigger:** a trace exported with `anim.trace.export` that reports
  overwritten records the investigation needed, or a consumer that wants a log
  on many entities at once and cannot afford 64 records each.

## Default decision log on character entities in dev builds

- **What:** the runtime plan says dev builds keep a short decision log on every
  Character-tier entity. Today a log is added by content, by the editor
  preview, or by `anim.trace <entity>`.
- **Where:** tiers are presets in the animation editor
  (`editor/animation_editor/src/authoring/AnimationRigRecipe.h`), not a runtime
  property, so the runtime has nothing to key the default on.
- **Trigger:** the rig or scene format gaining a declared per-entity diagnostic
  setting, or the decision-log depth work above, which settles how a short ring
  is stored.

## Engine-wide participation LOD tiers

- **What:** the animation runtime plan places its server participation tiers
  beside "the existing participation LOD tiers", which are proposed but not
  built. Animation's server skip is its own rule today: a machine that does not
  present poses skips a rig whose binding proves it drives no gameplay
  (`AnimBoundRig::DrivesGameplay`, `ShouldRunAnimationLogic`).
- **Where:** the proposal is `docs/action-adventure-core-runtime.md` §C and
  `docs/plans/engine-roadmap.md`; the animation rule is in
  `engine/include/anim/AnimRigBinding.h` and `engine/src/anim/AnimRigBinding.cpp`.
- **Trigger:** the first engine participation tier landing. Animation's skip
  then becomes a consumer of that tier rather than its own predicate.

## Animation editor surfaces

- **What:** the purpose-built surfaces the animation workspace still lacks
  after its move to lenses, cards and sheets. Missing: tag-set scenario inputs
  (the Facts card says a tag container is not driven); a slot-map precedence
  view (the Rules card lists the rows resolving a behavior, not how rows of
  several maps rank); a blendspace layout editor (the pad drives the axis
  facts, the layout edits as the blendspace's sheet); the take-A ghost drawn
  where the character stood rather than at the origin; a composed-motion view.
- **Where:** `editor/animation_editor/src/authoring/` (the lens cards and the
  pad's model), `editor/common/src/presenters/PlanePad`.
- **Trigger:** the animation editor UX polish pass that follows the editor
  consolidation.

## Layer weight fades

- **What:** a layer's weight steps when its weight rule changes; it does not
  fade over time.
- **Where:** layer weights resolve in `engine/src/anim/AnimContentSystem.cpp`
  and are applied by the pose pass.
- **Trigger:** the first rig whose upper layer visibly pops in or out, or the
  UX polish pass.

## Picking up another program's edits before save

- **What:** material and data documents take a file changed on disk while
  they are clean and hold off a change while they have their own; the level
  document does not notice until it is saved, when the conflict is refused and
  settled by keeping one side. Level files are not among the watched sources.
- **Where:** `DocumentSource::FileChangedOnDisk`; `LevelDocumentSource` in
  `editor/level_editor/src/workspace/`; the watched extensions in
  `editor/kyusu/src/app/ProjectSession.cpp`.
- **Trigger:** level files (`.sscene`, `.sworld`) being watched, for example
  by a scene-reload workflow.
## Sanitizers and benchmarks in CI

- **What:** the `asan` and `tsan` presets and the gated benchmarks run only by
  hand; CI runs neither.
- **Where:** `.github/workflows/ci.yml`, `CMakePresets.json`,
  `scripts/bench_animation.sh` (`test/runtime/AnimBench.cpp`,
  `test/editor/AnimationPreviewBench.cpp`) and the other gated benches.
- **Trigger:** a performance or memory-safety regression gate wanted in CI.

## Unsaved-document prompt polish

- **What:** closing, renaming or deleting an unsaved document asks Save,
  Discard or Cancel one document at a time with plain wording, and window focus
  loss still cancels typed text that has not been committed.
- **Where:** `editor/common/src/prompts/DocumentPrompts.*` (the questions; the
  shell's `PromptDialog` presents them).
- **Trigger:** the UX polish pass.

## Gameplay events on behaviors reached without a request

- **What:** the plan's invariant 12 forbids Gameplay-scope events (clip marks or
  lifecycle bindings) on behaviors reachable without a request. Binding does not
  enforce that half: gameplay events are produced only by the authority, and the
  remediation's requirements include gameplay events surviving a carry, which
  happens between fact-selected cyclic behaviors. Blendspace samples are required
  to share one gameplay track.
- **Where:** `AnimSelectorBinding.cpp` (`Validate`, beside the Reconstruct and
  root-motion pairings) and `docs/plans/animation-runtime.md`, invariant 12.
- **Trigger:** the owner's ruling on whether fact-selected behaviors may carry
  gameplay events.

## Animation performance pass

- **What:** the review's Phase D, left out of the correctness ticket: validating
  each rig's binding once per tick through a bindings epoch rather than once per
  run of equal handles in each system (`AnimRigRunCache` is where the epoch
  lands); an optional-column accessor on `Query` so systems stop reading
  same-chunk components through `TryGet`; rig-sized per-layer state in place of
  eight-layer arrays; selector gate digests over only the slots selectors read;
  per-behavior row ranges; skipping layers that contribute nothing to a pose; a
  release-mode check on archetype rows wider than a chunk; evicting
  `AnimRigBindings` entries for rigs no longer resident (a released rig's key is
  never looked up again, so this is memory, not correctness); World resource
  lookups hashing a type name, which a single-entity World such as the editor
  preview pays per system per tick; and a logic-tick bench for Simple and
  Character entities, which comes first.
- **Where:** `docs/plans/animation-runtime-review.md`, findings P1-P6 and P8,
  and "Remediation plan", phase D.
- **Trigger:** the performance and cleanup ticket that follows, and before any
  scene with hundreds of rigged entities ships.

## Level explorers walk their own trees

- **What:** `TreeOutline` (`editor/common/src/workspaces/`) is the one outline
  over preorder nodes, with closable branches, a filter that keeps ancestors,
  and the current node revealed. UI Preview's and Animation's explorers use it.
  Level's `WorldExplorer` and `PartitionOutline` still walk, filter and open
  their own trees.
- **Where:** `editor/level_editor/src/workspace/WorldExplorer.cpp`,
  `PartitionOutline.cpp`.
- **Trigger:** the next change to how either one pages, filters or opens
  branches moves it onto `TreeOutline` in the same change.

## Hand-rolled generational pools

- **What:** `HandlePool<Tag, T>` (`engine/include/core/handle/HandlePool.h`) is
  the one generational slot container; presentations use it. Older owners
  still keep their own slots, generation counters and free lists.
- **Where:** `VulkanImageService`, `VulkanBufferService`, `VulkanShaderCache`,
  `RenderTargetStore`, and `UiSurfaceTargetRenderFeature`'s bindings.
- **Trigger:** the next change to how any of them allocates, resolves or frees
  a handle moves that owner onto `HandlePool` in the same change.

## Per-window scale of view handles

- **What:** every authored surface takes its own window's scale, but the
  Level's in-view handle sizes (dial knobs, hit radii, stroke widths) read one
  process-wide `EditorUi::UiScale`, the primary window's; a Level detached to
  a display of another scale sizes its handles for the primary's.
- **Where:** `EditorUi::Px` in `editor/common/src/ui/EditorUiStyle.h`, read by
  `FaceCarveTool`, `ClipTool` and `ViewportDialPlacement`; set in
  `KyusuApp::UpdateShells`.
- **Trigger:** a mixed-DPI setup where a detached Level's handles read at the
  wrong size; the fix carries the window's scale on `ToolContext`.

## Frame scratch after the level closes

- **What:** a session that opens a level sizes the per-frame GPU scratch at
  64 MB at configure time (`KyusuApp::OnConfigure`); it stays that size after
  the Level workspace closes, since the scratch ring is fixed for the run.
- **Where:** `GpuFrameScratch`, `EngineGraphicsConfig::FrameScratchBytesPerFrame`.
- **Trigger:** a memory budget pass over the editor, or the scratch ring
  gaining a resize at a frame boundary.

## Level document dirty state is a flag

- **What:** `LevelDocumentSource` reports changes from the documents' dirty
  flags, so undoing back to the saved point still reads as changed, where the
  other sources derive it from the journal position.
- **Where:** `editor/level_editor/src/workspace/LevelDocumentSource.cpp`,
  `EditorDocument::MarkDirty`.
- **Trigger:** a report of a spurious save prompt after undoing to the saved
  state, or the level document gaining a saved-step marker in its
  `CommandStack`.

## One frame capture at a time

- **What:** `FrameImageCapture` holds one pending request, so arming a second
  capture (another presentation, or another frame) before the first is
  written replaces it.
- **Where:** `engine/include/graphics/vulkan/FrameImageCapture.h`,
  `Renderer::CaptureFrame`.
- **Trigger:** an unattended run that needs two windows, or two frames,
  captured in one process.

## Inset and bevel as gestures in the view

- **What:** an inset or bevel is staged from the selection card and tuned by
  its setting there (distance, width, segments), then applied or cancelled.
  Dragging in the view to set the distance, with the wheel for segments, is not
  built.
- **Where:** `editor/level_editor/src/workspace/ElementVerbs.cpp` stages them
  through `PendingElementEdit`; a gesture would drive the same setting.
- **Trigger:** the owner's walkthrough at the explorers checkpoint, or the
  first report that tuning an inset from the card is slower than dragging it.

## Selection actions listed under cards outside the Level

- **What:** a selection looked at through the context wheel has its actions on
  the wheel and the palette, never in a pile under its sheet. Only the Level
  has the wheel, so the other workspaces' cards (Animation's lens and document
  cards, Data's asset sheet, the Materials texture card) still
  list `WorkspaceSelectionFacet::Actions` under their rows.
- **Where:** `ShellWindow::PublishSelection` lists them unless the facet is
  `OnRequest`; the context wheel is `actions/ContextMenuModel`, opened by
  `LevelWorkspace` on Space.
- **Trigger:** any other workspace taking the context wheel, which then sets
  `OnRequest` and moves its actions there, or a component of those cards
  naming its own actions the way the Level's components do.

## Adapter-drawn components across a multi-selection

- **What:** several selected objects are edited as one, except a component
  whose adapter draws its own rows (a brush's modifier stack, the world dock
  and gate adapters): across two or more holders it shows a note and is
  edited one object at a time, because two stacks or lists have no one stack
  to show.
- **Where:** `ComponentPropertySource::Nodes` and `SoleHolder` in
  `editor/level_editor/src/authoring/ComponentPropertySource.cpp`;
  `IEditorComponentAdapter` in `EditorComponentAdapter.h`.
- **Trigger:** an adapter whose rows mean the same thing on every holder --
  a single setting rather than a list -- which would then say it presents
  several together, or a designer asking to apply one stack to many brushes.

## Previewing a material on the selected faces

- **What:** picking a material on the Shelf (M) does not show it on the
  selected faces before committing; a click puts it in hand and Enter, a double
  click or CHOOSE applies it as one step.
- **Where:** `editor/level_editor/src/workspace/MaterialExplorer.cpp` is handed
  each pick; a preview would be a pending edit through the command stack's
  pending scope, cancelled by the next pick or when the Shelf goes.
- **Trigger:** the owner's walkthrough at the explorers checkpoint, or a report
  that choosing a material takes trial applications and undos.

## Dragging a prefab into the view

- **What:** a prefab is placed by choosing it on the Shelf (P), which takes up
  the Place tool, then clicking in the view. Pressing on a tile and dragging it
  into the view as the same gesture is not built, and the Place tool turns a
  placement by its yaw setting rather than by a key while hovering.
- **Where:** `editor/level_editor/src/document/tools/PlaceTool.cpp`; the Shelf
  (`editor/common/src/shell/Shelf.cpp`) would hand the press over to the
  view's input router.
- **Trigger:** the owner's walkthrough at the explorers checkpoint.

## Reordering and reparenting by dragging in the outline

- **What:** the World explorer parents the selection under its primary
  (Ctrl+P) and clears parents (Alt+P), but rows cannot be dragged onto another
  row or between rows to reparent or reorder, as the ImGui hierarchy allowed.
- **Where:** `editor/level_editor/src/workspace/WorldExplorer.cpp` (a choice
  naming a drop target), `MakeReparentEntitiesCommand`'s reorder form, and the
  shell's outline rows.
- **Trigger:** a designer ordering siblings, which no action can do today.

## A picture of the shadow atlas

- **What:** a selected light's card reads its shadow tier (and any downgrade),
  its slot's state and how full its pool and the spot atlas are, and a light the
  budget left unshadowed is a problem. The ImGui lighting panel also drew the
  atlas itself, every live tile at its place and size with its guard band and
  age; nothing draws that picture now.
- **Where:** `editor/level_editor/src/workspace/LightingDiagnostics.cpp` reads
  the same `ShadowResidencyReadout`; the deleted drawing is
  `LightingPanel::DrawAtlasMap` in commit 95a45376. A picture would be a host
  image or view marks, not a new readout.
- **Trigger:** an atlas packing or tier downgrade that the card's tier and pool
  lines cannot explain, met while tuning shadow budgets or the allocator.

## A texture cartridge for the slot in hand

- **What:** the Materials workspace sets a texture slot by choosing among the
  source textures on the Shelf from the card's field, and inspects a texture by
  opening it from the Shelf. The plan's cartridge -- the texture the focused
  slot holds, in the lower right, with the Shelf a click away and a pick
  loading it into that slot -- is not built.
- **Where:** `editor/material_editor/src/MaterialWorkspace.cpp` would offer a
  resources facet (`WorkspaceResourceFacet`, the Level's cartridge) for the
  focused texture slot of `MaterialProperties`; the shell already presents it.
- **Trigger:** the owner's walkthrough of the Materials workspace, or a material
  with several texture slots being filled one after another.

## The movement response curves

- **What:** the movement profile's Response section gives the jump and speed
  response as numbers (jump height, time to apex, hang time, hop distance, top
  speed, time to top speed, stopping distance). The ImGui panel also plotted
  the jump arc's height and the speed ramp over time; nothing draws those
  curves now.
- **Where:** `editor/data_editor/src/movement/MovementPreviewProperties.cpp`
  has the samples (`MovementJumpArc::Samples`,
  `MovementSpeedResponse::Samples`); a curve would be a small host image or a
  row the sheet draws as a polyline.
- **Trigger:** tuning a jump's shape rather than its height and hang time, or
  the Animation workspace's fact plots (M13), which need the same drawing.

## Engine content on the Shelf

- **What:** the Shelf lists the project's content roots only. An asset field
  whose candidates include engine-provided content (the engine's authored
  assets mounted beside the project) offers those candidates nowhere: the
  pick shows the project's files among them and NONE. The UI Preview's
  document library stays its own outline (D) for the same reason -- most of
  what it previews is the engine's and the editor's own UI.
- **Where:** `ShellWindowServices::ContentRoots` (from `KyusuApp::ContentRoots`)
  feeds `Shelf::Rescan`; `ShellWindow`'s sheet pick hands the field's
  candidates to `ShelfPick::Only`; `UiPreviewWorkspace::BuildLibrary` adds the
  editor and engine roots.
- **Trigger:** a field whose only sensible choice is an engine asset, or a
  designer opening engine content from the Shelf. The Shelf would then take
  read-only roots, shown as their own top-level folders, and the UI Preview
  library would fold into it.
