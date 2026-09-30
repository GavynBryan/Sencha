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

- **What:** purpose-built surfaces the animation editor still lacks. Every field
  is editable today through the Document panel's schema form. Missing:
  tag-set scenario inputs and the predicate text form; a behavior policy
  inspector linked to the rules and rows that reach it; slot-map precedence, a
  blendspace layout editor and fact derivations; lifecycle bindings in their
  own inspector; event marks on the shared timeline; the take-A ghost drawn
  where the character stood and a composed-motion view.
- **Where:** `editor/animation_editor/src/ui/`.
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

## Hand-rolled generational pools

- **What:** `HandlePool<Tag, T>` (`engine/include/core/handle/HandlePool.h`) is
  the one generational slot container; presentations use it. Older owners
  still keep their own slots, generation counters and free lists.
- **Where:** `VulkanImageService`, `VulkanBufferService`, `VulkanShaderCache`,
  `RenderTargetStore`, and `UiSurfaceTargetRenderFeature`'s bindings.
- **Trigger:** the next change to how any of them allocates, resolves or frees
  a handle moves that owner onto `HandlePool` in the same change.

## Authored UI in detached windows

- **What:** the engine places authored surfaces on any window
  (`UiService::SetSurfaceWindow`, per-window routing, capture and render
  feature), but Kyusu still creates its surfaces for the primary window, so the
  level's cook-profiles modal and inspector surface appear on the main window
  when the Level workspace is detached.
- **Where:** `LevelWorkspace`'s `AuthoredSurface`, sized from `PrimaryWindow`.
- **Trigger:** the Shoji shell's per-window controller (`ShellWindow`), which
  owns each window's surfaces; the Level workspace's surfaces move onto it.

## Detached window layout write-back

- **What:** a detached window starts from the primary's in-memory layout
  (`EditorUiFeature::InitImGui`), but dock changes made in it are not written
  back, so a workspace rearranged while detached comes home in its old layout.
- **Where:** `editor/common/src/ui/EditorUiFeature.cpp`, detached `InitImGui`
  (`io.IniFilename = nullptr`).
- **Trigger:** the first report of a lost detached arrangement, or the 3D
  workspace manager, which will want placement to persist.

## Per-window UI scale

- **What:** the UI scale is resolved once from the primary window's display
  and shared by every window's style and font atlas; a detached window on a
  display of another scale draws at the primary's.
- **Where:** `ResolveUiScale` and `EditorUi::UiScale` in
  `editor/common/src/ui/EditorUiFeature.cpp` and `EditorUiStyle`.
- **Trigger:** a mixed-DPI setup where a detached window reads at the wrong
  size; the fix is an atlas per scale, since fonts bake at a size.

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

## Common properties across a multi-selection

- **What:** the selection card for several objects names them and offers the
  verbs that apply to all of them, but shows no properties: fields they share,
  with mixed values marked, are not presented.
- **Where:** `editor/level_editor/src/workspace/SelectionSummary.cpp` offers
  details for one object only; `ComponentPropertySource` reads the primary
  selection.
- **Trigger:** a designer editing the same field on several selected objects
  at once.

## Previewing a material on the selected faces

- **What:** hovering a tile in the material explorer does not show that
  material on the selected faces; picking puts it in hand and putting it to
  use (a double click) applies it as one step.
- **Where:** `editor/level_editor/src/workspace/MaterialExplorer.cpp`; a
  preview would be a pending edit through the command stack's pending scope,
  cancelled when the pointer leaves the tile or the explorer closes.
- **Trigger:** the owner's walkthrough at the explorers checkpoint, or a report
  that choosing a material takes trial applications and undos.

## Dragging a prefab into the view

- **What:** a prefab is placed by choosing it in the explorer, which takes up
  the Place tool, then clicking in the view. Pressing on a tile and dragging it
  into the view as the same gesture is not built, and the Place tool turns a
  placement by its yaw setting rather than by a key while hovering.
- **Where:** `editor/level_editor/src/document/tools/PlaceTool.cpp`; the shell's
  explorer tray (`editor/common/src/shell/ShellWindow.cpp`) would hand the
  press over to the view's input router.
- **Trigger:** the owner's walkthrough at the explorers checkpoint.

## Reordering and reparenting by dragging in the outline

- **What:** the World explorer parents the selection under its primary
  (Ctrl+P) and clears parents (Alt+P), but rows cannot be dragged onto another
  row or between rows to reparent or reorder, as the ImGui hierarchy allowed.
- **Where:** `editor/level_editor/src/workspace/WorldExplorer.cpp` (a choice
  naming a drop target), `MakeReparentEntitiesCommand`'s reorder form, and the
  shell's outline rows.
- **Trigger:** a designer ordering siblings, which no action can do today.
