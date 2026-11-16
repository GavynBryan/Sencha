# Deferred work

Work deliberately left for later. Each entry names what was deferred, where the
code lives, and the concrete trigger that makes it due. When a trigger lands,
do the work and delete the entry.

## Continuous sensor trajectories

- **What:** sensor observations query exact endpoint shapes and cast the linear
  displacement using the final orientation. They do not reconstruct slides,
  stair steps, multiple character substeps or angular motion. Moving sensors
  are sampled at their new pose rather than swept through activators.
- **Where:** `engine/src/physics/SensorShapeQuery.cpp`,
  `CharacterMover::ObserveSensors` and `PhysicsWorld::ObserveBodySensors`;
  the supported contract is in `docs/physics/sensor-overlaps.md`.
- **Impact:** fast linear character/body crossings are covered; a curved path
  can differ from its chord, and an angular or moving-volume crossing between
  samples can be missed. Endpoint occupancy and ordinary Enter/Stay/Exit
  remain exact. This is not a continuous-trajectory guarantee.
- **Trigger:** content requiring detection along a curved, angular or moving
  sensor trajectory, such as a fast sliding actor or a sweeping volume. That
  consumer needs bounded motion-segment observation at the backend movement
  boundary, retaining the same Jolt world and overlap lifecycle.

## Runtime collider configuration reconciliation

- **What:** `RigidBodyBinding` captures `Collider.Shape`, `Mesh`, `IsTrigger`,
  derived collision layer, and most `RigidBody` configuration when creating a
  body. Value edits do not recreate or update that body, even if a structural
  change causes another reconciliation pass. Only kinematic pose and dynamic
  gravity scale have live push contracts today. The entity's scale is read
  into the shape (`ScaledCollisionShape`) at the same moment, so a resident
  entity rescaled at runtime keeps the body it was built with, and a
  `RigidBody` added to an entity whose static body already exists leaves it
  static.
- **Where:** `engine/src/physics/RigidBodyBinding.cpp` (`Reconcile`,
  `SyncToPhysics`), `engine/include/physics/components/Collider.h`.
- **Impact:** changing `IsTrigger` on a resident entity is unsupported; the ECS
  value and Jolt sensor/layer can disagree. Configuration supplied before binding
  a fresh simulation is honored. This observation PR does not promise live collider
  editing and must not add a sensor-specific body recreation path.
- **Trigger:** the first runtime collider configuration edit or live Kyusu
  physics hot reload. Implement generic configuration reconciliation in
  `RigidBodyBinding`: retain the applied configuration per owned body, detect
  changed collider/body configuration with cached changed queries, update or
  recreate through the existing binding owner, preserve supported dynamic state,
  refresh dependent constraints and invalidate observation pairs without synthetic
  teardown exits. Cover shape,
  sensor/layer, motion, mass, mesh ownership, scale, and residency together.

## Authoring a rigid body in a scene

- **What:** `RigidBody` declares no schema, so a level cannot say a collider's
  body is kinematic or dynamic: every authored collider is static world
  geometry unless code adds the component. Loss Function's `Sway` adds a
  kinematic one to the entity it moves, before its first physics step.
- **Where:** `engine/include/physics/components/RigidBody.h`; the Kyusu
  collider rows in `editor/level_editor/src/authoring/ColliderEditorAdapter.cpp`.
- **Trigger:** the first authored prop that falls, or a mover that is not a
  game's own component. Give `RigidBody` a schema whose persisted fields are
  motion, mass, gravity scale and damping (velocities stay runtime state), a
  scene chunk, and a motion choice beside the collider's rows.

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

## Hand-rolled generational pools

- **What:** `HandlePool<Tag, T>` (`engine/include/core/handle/HandlePool.h`) is
  the one generational slot container; presentations use it. Older owners
  still keep their own slots, generation counters and free lists.
- **Where:** `VulkanImageService`, `VulkanBufferService`, `VulkanShaderCache`,
  `RenderTargetStore`, and `UiSurfaceTargetRenderFeature`'s bindings.
- **Trigger:** the next change to how any of them allocates, resolves or frees
  a handle moves that owner onto `HandlePool` in the same change.

## A target texture per viewer

- **What:** a target texture is one picture per name, so every material naming
  `target:health` shows the same health. Two local players each holding a rifle
  would see the same screen. The route is a material per viewer, substituted
  into each viewer's view model through a mesh override, each naming its own
  target.
- **Where:** `TargetTextureTable` (`render/TargetTextures.h`), and Loss
  Function's `HealthTankHud`, which draws the local player's health.
- **Trigger:** split screen, or a second local view of any kind. The engine has
  one local subject and one active camera today.

## Mips for a target texture seen from afar

- **What:** a target texture has one level, sampled linearly. Close up, as on a
  view model, that is right; a screen seen across a room minifies it and
  shimmers.
- **Where:** `UiTextureTargets::BindingFor`'s target description.
- **Trigger:** the first target texture shown on a prop placed in a level, where
  it can be seen from a distance. Generate the chain after a redraw, which is
  rare, rather than every frame.

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

## Turning a placement by a key while it is placed

- **What:** a prefab placed with the Place tool turns by its yaw setting (the
  tool card's 0/90/180/270), not by a key while the ghost hovers; one dragged
  from the Shelf into the view lands unturned, because the drop repeats the
  tool's surface-then-grid pick instead of placing through the tool.
- **Where:** `editor/level_editor/src/document/tools/PlaceTool.cpp`;
  `LevelWorkspace::DropAsset`.
- **Trigger:** the owner's walkthrough at the explorers checkpoint, a designer
  placing many turned props, or `claude/place-preview` landing: it hands the
  drop to the tool as a carry and resolves both.

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

## References kept current while the Shelf is up

- **What:** the reference index reads the project's documents again only as
  the Shelf rises (changed ones only: 0.2 ms after the first read, which took
  47 ms for 242 files in a debug build). A file saved while the Shelf stays up
  or pinned shows its old uses until the Shelf rises again. UI documents
  (`.rml`, `.rcss`) and legacy `.sworld` files name what they use by relative
  path, not `asset://`, and are not read.
- **Where:** `editor/common/src/project/ReferenceIndex.cpp` (`Refresh`), called
  by `Shelf::Rescan`; `ScanUiSource` (cook layer) already reads UI documents'
  references.
- **Trigger:** a pinned Shelf showing stale uses after an edit, a project whose
  first read is a felt hitch on raising the Shelf (then the first read moves to
  `AsyncTaskQueue` and commits at the frame boundary), or a person looking
  for what uses a stylesheet.

## Emptying the project's trash

- **What:** a file trashed from the Shelf goes to `<root>/.trash/<step>/` so
  Ctrl+Z can bring it back, and nothing ever empties that folder. Leaving the
  journal (closing Kyusu) leaves the files there for good.
- **Where:** `editor/common/src/project/ProjectFileSteps.cpp` (`Trash`);
  `.trash` is a hidden folder (`IsHiddenContentFolder`), so no scan, import or
  watcher sees it.
- **Trigger:** a project whose trash is a felt size, or a person asking where
  trashed files went. Then the trash empties what no journal step can reach
  any more (on exit, or a Shelf verb), or hands files to the OS trash.

## Cooked artifacts a move or trash leaves behind

- **What:** moving or trashing an imported source drops its cooked index entry
  and registry records (`ForgetImportedSource`), but its artifact bytes stay
  under `.cooked/`: content-hash dedup may share them with another source, so
  they are never deleted by hand.
- **Where:** `engine/src/assets/cook/ImportOnDemand.cpp`
  (`ForgetImportedSource`); `PruneOrphanedGeneratedArtifacts`
  (`assets/cook/CookPrune.h`) is the prune that knows which bytes no entry
  names, run only by tests today.
- **Trigger:** a project's `.cooked/` growing visibly from moves, or the batch
  cook gaining a prune pass; then the prune runs over every root's index.

## Moving a file an open material or data document names

- **What:** a material or data document writes the references it read back as
  it read them, so the Shelf refuses to move a file one of them names while it
  is open: saved after the move, it would name the file where it was. A level
  settles every name through the registry when it saves, and moves freely.
- **Where:** `MaterialWriter` (texture slots) and `DataDocument::Write` (the
  working root as read); `DocumentSource::KeepsNamesAsRead`, which
  `MaterialDocumentSet` and `DataDocumentStore` answer by default.
- **Trigger:** a person stopped by the refusal with a material or rig open, or
  either writer settling its references (`AssetRegistry::Current`) for another
  reason; then their sources stop keeping names as read.

## Carrying a tile from the shelf's window into another

- **What:** the Shelf in a window of its own opens what it holds in the
  workspace's window, but a tile dragged from it cannot be let go over another
  window's view or fields: a drag lives in the document it began in, and each
  window has its own.
- **Where:** `editor/common/src/shell/Shelf.cpp` (Carry, `TakeDrop`) and
  `ShellWindow` (`shell_sheet_drop`, `DropAsset`); the engine's per-window
  authored UI contexts (`engine/src/ui/UiRuntime.cpp`).
- **Trigger:** a person working with the Shelf on a second display who wants
  to drop from it, which then needs a drag the application holds across
  windows rather than one document does.

## Mesh stand-ins

- **What:** a missing material or texture stands in as the pink and black
  checker (`docs/assets/pipeline.md`, Decision P). A missing static or skinned
  mesh still fails its load, so the scene field naming it fails as before.
- **Where:** a kind opts in with `AssetKindRegistration::StandIn`
  (`engine/include/core/assets/AssetKindRegistry.h`), registered in
  `RuntimeAssets` (`engine/src/assets/runtime/RuntimeAssets.cpp`) from
  `engine/include/assets/runtime/AssetStandIns.h`.
- **Why not now:** what a missing mesh is has product consequences a material
  does not: its bounds decide culling and placement, its sections choose the
  entity's materials (so a plain cube would draw in the entity's own material,
  not conspicuously), and collision cooked from it would collide with a shape
  nobody authored.
- **Trigger:** content that ships a scene whose mesh can go missing (a renamed
  or deleted model in a project), or the owner choosing the mesh stand-in's
  shape, its material, and whether it collides.

## Same-named stylesheets in different UI packages

- **What:** two packages that each carry a stylesheet under the same virtual
  name (both `menu.rcss`, with different rules) share one: the document engine
  caches a sheet by the name it was linked by, so the second package's
  documents are styled by the first's. Found while writing
  `UiInput.AReleaseOverAnotherSurfaceEndsTheDragItBelongsTo`, whose two
  packages now name their sheets apart.
- **Where:** the runtime's file interface over package blobs
  (`engine/src/ui/UiRuntime.cpp`, how a package's blobs are named to RmlUi);
  the fix is to qualify each blob's name by its package so identity follows the
  package, not the leaf name.
- **Why not now:** no shipped content collides (the editor's documents share
  one kit, whose sheets are the same files), and qualifying names touches how
  links resolve, the cook's dependency records and hot reload of a shared sheet.
- **Trigger:** a game or the editor shipping two packages whose stylesheets
  share a name but not their rules, or the authored-UI hot reload work touching
  how blobs are named.

## Placements standing on a surface

- **What:** the Entity card's planned "align to surface": a placement standing
  along the normal of the surface under it rather than upright.
- **Where:** `ResolvePlacementSite` (`editor/level_editor/src/document/placement/PlacementResolver.cpp`)
  makes every site upright; `SiteOnPlane` and the turn already take any normal
  (`docs/editor/placement.md`, Turning).
- **Trigger:** the Entity card gaining the setting, or the first prefab meant
  to hang on a wall or a ceiling.

## A trigger pulled and released inside one frame

- **What:** a gamepad trigger bound as a button is judged against its threshold
  once per rendered frame, on the frame's resting position, so a pull and release
  that both happen between two frames produces no edge. Button transitions are
  logged in order and never lose a tap; analog positions are not.
- **Where:** `UpdateAnalogThresholds` in `engine/src/input/InputActionResolve.cpp`
  reads `InputFrame::GamepadAxes`, which holds positions only. An axis event log
  beside `InputFrame::Transitions` would let attribution replay crossings in order.
- **Trigger:** a measured lost trigger tap (a semi-automatic weapon on a trigger
  that drops shots at the frame rates players run).

## Leases and cosmetic verbs through the generated authored API

- **What:** `SENCHA_VERB` declares an instant gameplay verb that only the authority
  runs. It has no way to declare a lease (`VerbKind::Lease`, with its release
  half), a cosmetic verb, or a prediction class, so a game declares those by hand
  as the engine's own are (`engine/src/effects/EffectVerbs.cpp`).
- **Where:** the annotations are `engine/include/authored/AuthoredAnnotations.h`;
  the generator is `tools/component-codegen/ComponentCodegen.cpp`; what it emits
  is read by `AuthoredVocabularyScope` and `BindAuthoredApi`
  (`engine/include/authored/AuthoredApi.h`).
- **Trigger:** the first game module that declares a lease, a cosmetic verb or a
  predicted verb. The Loss Function module (AbilityKit 2, Stage 10) is the known
  candidate, if any of its abilities needs an operation the engine does not offer.

## An animation hold whose holder is gone

- **What:** an `anim.hold` lease whose owner's entity ends without releasing it is
  reported as orphaned and stays held, the existing rule for any Held request a
  producer forgot. Plan §11 asks lease providers to end such holds themselves (the
  pull rule), as effects already do.
- **Where:** `AnimRequestOperations::Hold` (`engine/src/anim/AnimRequestVerbs.cpp`)
  and the orphan report in `NoteAnimRequestOutcomes`
  (`engine/src/anim/AnimContentSystem.cpp`).
- **Trigger:** an ability that holds animation on an entity other than the actor
  running it. An actor's own holds die with it, so CTX Charge and every Stage 10
  ability are covered; the first ability that poses another entity is not.

## Dilating a character in a game-defined locomotion mode

- **What:** a dilated character is stepped through `StepScaledCharacterTick`, which
  implements only the free mode; a character in a mode a game registers keeps the
  bulk path and runs at simulation speed whatever its scale.
- **Where:** `engine/src/movement/CharacterTickStep.cpp` (`CharacterTickModeSupported`)
  and `DilatedCharacterStepSystem`.
- **Trigger:** the first game mode whose characters are time-scaled, which will also
  need that mode's kernel shared with replay.

## Dilated characters meeting characters, and bodies striking them

- **What:** the AbilityKit plan's actor-time interactions include two characters at
  different scales colliding and an undilated body striking a dilated character
  (`CharactersAtDifferentScalesCollideConsistently`,
  `BodyStrikingADilatedCharacterUsesSimulationRate`). Neither happens at any scale
  today: the character mover sets up no character-versus-character collision, and
  a character's inner body is on `CharacterBody`, a layer queries strike and that
  pairs with nothing, so characters pass through each other and rigid bodies pass
  through characters.
- **Where:** `engine/src/physics/CharacterMover.cpp` (`CharacterMover::Move`, the
  `CharacterVirtual` settings) and `test/runtime/ActorTimeInteractionTests.cpp`.
- **Trigger:** a character's body taking part in contacts (pairing
  `CharacterBody` with `Moving`) or character-versus-character collision; the two
  tests are written with that change, against dilated characters as well as
  undilated ones.

## Ending an ability on an authored event

- **What:** `cancel_on` entries that end a running activation when an authored
  event is published from a source the entry names (the plan's "authored event
  from a source filter", `docs/plans/abilitykit-2.md` §3.5). Conditions,
  `target_lost` and the always-on triggers are delivered; this one is not.
- **Where:** `AbilityPass::Sweep` (`engine/src/abilities/AbilityPass.cpp`) and the
  condition kinds in `abilities/AbilityDeclaration.h`; events come through
  `AuthoredEventDispatcher`, which drains once per fixed tick after fixed logic.
- **Why not now:** the drain runs after the ability pass and is not journaled or
  replicated, so whether such a cancel is seen the next tick, is authority-only, or
  is predicted from a client's own events is a design choice no consumer has made;
  no planned content (the normative charge, CTX modes, Vector abilities, Overclock)
  ends on an event.
- **Trigger:** the first ability content that must end when an event is published
  (a stagger announced by a hit, an interrupt published by a script), which also
  decides the timing and prediction rule above.

## Showing a predicted commit's costs as pending

- **What:** plan §5.2 has a client show what a guess would pay as a display overlay
  keyed by activation, cost group and pulse, cleared when the authority's word
  arrives. A guess validates its commit and pays nothing; no overlay is kept.
- **Where:** `AbilityPass::Commit` (`engine/src/abilities/AbilityCommit.cpp`), which
  knows the groups a guess would pay.
- **Trigger:** the first HUD that shows a predicted pawn's ammo or energy, which is
  what reads the overlay.

## Locomotion facts for a pawn this machine only receives

- **What:** a remote pawn's locomotion animation reads facts its machine is not
  sent: velocity and support state are owner-only fields of `KinematicState` and
  `SupportState`, and only tags declared `replicate_to_all` reach it. Its
  locomotion selection therefore runs on stale facts (plan D18). Ability cues,
  presentation animation and authoritative animation requests do reach it, and are
  what AbilityKit's remote acceptance covers; remote locomotion quality is not
  claimed.
- **Where:** the facts' replication policy in
  `engine/include/movement/components/CharacterFacts.h`, and the fact providers that
  read them (`docs/gameplay/animation.md`, "Facts").
- **Trigger:** the first remote pawn whose locomotion must read right on another
  machine, such as Loss Function's deathmatch, which also decides whether facts are
  sent, derived on the receiver from interpolated motion, or both.

## Spatial queries

- **Parallel candidate evaluation.**
  - *Code:* `engine/src/spatial/candidates/`.
  - *What's missing:* runs are serial and owner-thread only. Running them in parallel needs a concurrent-read path through ECS queries (`World::QueryDepth`, `Query`'s lazy archetype cache), per-worker scratch, and a non-allocating dispatch.
  - *Trigger:* a measured per-tick evaluation cost over ~1 ms in a real encounter.
- **Native candidate operations from game modules.**
  - *Code:* `engine/include/spatial/candidates/CandidateCatalogs.h`.
  - *What's missing:* the catalogs hold only the engine's operations. Game questions go through the `authored_query` measure.
  - *Trigger:* a game needs a native batch measure that `authored_query` is too slow for. The catalogs' names and contract revisions make this an addition, not a redesign.
- **Top-N bound pruning.**
  - *Code:* `CandidateEvaluator::RunCriterion`.
  - *What's missing:* skipping expensive measures on candidates that can no longer reach the top N, returning exactly what unpruned evaluation returns.
  - *Trigger:* profiling shows expensive measures spent on hopeless candidates.
- **Clearance measure.**
  - *What's blocking it:* `PhysicsQueries::SweepShape` and `OverlapShape` build a backend shape on every call, and `OverlapShape` fills a `std::vector`, so per-candidate use would allocate. Navigation's build profile already guarantees standing room against static geometry.
  - *Trigger:* `PhysicsQueries` accepts cached shapes and caller-owned output.
- **Multi-tick evaluation scheduler.**
  - *What's missing:* a budgeted scheduler that calls the same `Evaluate` on a subset of callers per tick.
  - *Trigger:* measured per-tick cost of real encounters exceeds budget.

## Sight candidate selection

- **What:** a spatial candidate query for sight. Every sensor is tested
  against every target for range and cone each tick, O(sensors x targets)
  before any ray; the per-observer ray cap bounds rays, not this.
- **Where:** `SightScan::Run` in `engine/src/perception/SightScan.cpp`, which
  gathers targets once per tick in id order.
- **Trigger:** `SightStats::Candidates` in a representative encounter putting
  the perception system over its ~1 ms share of the fixed tick, measured with
  `frame.trace.output`. Distance-ordered ray grants belong with it if far
  targets in view crowd out near ones.

## Shell neighbour lookup

- **What:** a shelled space finds the spaces and openings near it by visiting
  every entity and measuring bounds, and every space does so each time the
  level checks for stale walls, which is after every edit. That is
  O(spaces x entities x volume vertices) per edit.
- **Where:** `NeighboursOf` and `OpeningsNear` in
  `editor/level_editor/src/document/SpaceWalls.cpp`, reached through
  `SpaceShellInputs` from `RebuildStaleSpaceWalls`.
- **Trigger:** a level with more than about a hundred shelled spaces, or an
  edit whose profile shows `SpaceShellInputs`. The fix is a retained
  per-scene index of shelled volumes' and openings' world bounds,
  invalidated by their placement keys.

## Tool commits through the level target

- **What:** decision D5 of `docs/plans/level-spatial-model.md`: every mutation
  is a change set through the level target, so a designer's tool and an
  agent's operation edit alike. The move gizmo's transform commits, the Space
  tool and the Brush tool do; the rest of the tools still commit their own
  commands. The clip tool already shares its cut with `brush.clip`
  (`brush/BrushClipCut`), and the delete and duplicate verbs share their
  branch rules with `entity.*` (`document/EntityBranches`); only their
  commits remain.
- **Where:** `BrushManipulationSink` (`CommitMeshes`, `CommitMesh`,
  `CommitSplits`, `CommitDuplicate`), the carve, clip and edge-cut tools,
  `PendingBridgeEdit` (`DeferredCreateBrushCommand`), and the selection verbs
  in `editor/level_editor/src/workspace/SelectionActions.cpp`.
- **Trigger:** each tool moves the next time its commit path is changed.

## Command labels

- **What:** the level spatial model plan (M1) asks for undo steps to carry
  labels, so an accepted proposal reads as its label in the undo history.
  `ICommand` has no label, and nothing shows undo history: the tray lists
  proposals, not steps, so a label would have no reader.
- **Where:** `editor/common/src/commands/ICommand.h` and `CommandStack`;
  `ApplyChangeSetCommand` would carry the proposal's label.
- **Trigger:** an undo history display, or an "Undo <what>" menu label.

## Cross-zone reachability moves onto the cross-zone planner

- **What:** `world.reachable` and `world.bypasses`
  (`editor/level_editor/src/operations/WorldReachability.cpp`) walk a world
  zone by zone themselves: within a zone with `NavCollectReachable`, between
  zones through the authored docks (a dock's mouth reached on one side, its
  landing projected on the other). That is zone-graph search, which the
  roadmap's hierarchical cross-zone planner (`docs/plans/engine-roadmap.md`,
  Track A item 6) will own. Its design is not settled, so the analysis does
  not wait for it.
- **Where:** `WorldReachability.cpp` (`Walk`, `Mouth`, `Landing`).
- **Trigger:** the cross-zone planner lands. The analysis then asks it rather
  than walking docks itself, keeping its sure and possible scenarios, gate
  assumptions and one-way drops; world links get positions through the same
  work (`navigation-core.md`, section 7) and stop being unconsidered.

## Navigation agents

From `docs/plans/navigation-agent.md` (section 11). The agents themselves are
described in `docs/navigation/runtime.md`, "Agents".

- **A traversal carried out by an ability or a lease.**
  - *What:* an executor that starts an ability or acquires a lease for a
    crossing and finishes when it does.
  - *What's blocking it:* an `AbilityRequest` carries no position and is not
    tied to the record it starts, and a lease that completes cannot say whether
    it succeeded.
  - *Where:* `navigation/NavTraversal.h`, `abilities/AbilityRequestQueue.h`,
    `VerbDefinition::Completes`.
  - *Trigger:* the first crossing that needs animation or an ability (a ladder,
    a mantle, a door).
- **Binding traversal kinds to executors from data.**
  - *What:* today a game module calls `RegisterNavTraversal`.
  - *Trigger:* the first content that binds a kind without module code, likely
    the roadmap's data-authored enemy.
- **`navigation.arrived` / `navigation.failed` authored events.**
  - *What:* push delivery of an order's end; the queries answer it by polling.
  - *Trigger:* the first push consumer (the script runtime or LevelFlow).
- **Facing the direction of travel.**
  - *What:* the follower writes no facing; `LookOrientation` with `AimFacing`
    owns rotation.
  - *Trigger:* the first NPC content whose body must face its motion, or a
    third-person template, which needs the same thing for the player.
- **Telling a change behind an agent from one ahead of it.**
  - *What:* `NavValidateRoute` reports only that a dependency changed, so a
    change to a tile already walked past stops the agent for a replan.
  - *Where:* `NavValidateRoute` (`navigation/NavigationQuery.h`) and the sweep in
    `NavAgentSystem.cpp`.
  - *Trigger:* a measured stop or repath rate from changes behind agents in real
    content.
- **A parallel follow pass.**
  - *What:* steering runs serially.
  - *Trigger:* the follow pass measured over ~1 ms in a real encounter.
- **Cross-zone orders.**
  - *What:* an agent in the persistent partition fails with `NoZone`, and a
    target in another zone fails with `DestinationInOtherZone`.
  - *Trigger:* the cross-zone planner (roadmap Track A item 6).
- **Navigation for a client-predicted pawn.**
  - *What:* click-to-move for the local pawn. The authority re-derives a pawn's
    intent from its actions, so the order itself would have to travel.
  - *Trigger:* a game that navigates the local pawn.
- **Partial routes.**
  - *What:* "get as close as you can" to an unreachable point;
    `NavStatus::Partial` is declared but nothing returns it.
  - *Trigger:* a consumer that cannot use a reachable stand-in found with
    `NavCollectReachable` or candidate evaluation.
- **`nav.agent.goto <agent> aim`.**
  - *What:* sending an agent to the point under the local player's aim.
    `goto ... me` and explicit points exist.
  - *Where:* `engine/src/navigation/NavAgentConsole.cpp`; it needs the physics
    world's `PhysicsQueries::Raycast`.
  - *Trigger:* the first hands-on session that wants it.

## Engine-scope borrowers of the run scope

- **What:** a few engine-scope objects keep pointers into run-scope owners
  between the end of `Engine::Run` and `Engine::Shutdown`. The default render
  pipeline's features hold the content stack's mesh and material caches, and
  scheduled systems (input mapping, verb relays, responses, navigation agents)
  hold its `DataAssetCache`; responses hold its `AssetRegistry` too. None dereferences them after the run; ASan runs of every exit
  path are clean. The shell and the authored UI feature were the cases that did,
  and are now released by `Run` (`docs/architecture/engine-lifetime.md`).
- **Where:** `DefaultRenderPipeline` (`engine/src/app/DefaultRenderPipeline.cpp`),
  `RegisterInputSystems`, `RegisterVerbRelaySystem`, `RegisterResponsesSystem`, and the systems
  `EngineSchedule::Shutdown` tears down in `Engine::Shutdown`.
- **Trigger:** the first teardown hook in any of them that touches what it
  borrows. That makes the borrow a real use-after-free, so the borrower is
  released in `Run`'s teardown in that change.

## Approach direction for sensor responses

- **What:** a sensor response cannot ask which way its activator crossed the
  volume (a one-way gate). A sensor sample reports occupancy transitions, not a crossing
  direction, and reading the activator's velocity afterwards would be a second,
  movement-specific source of the same fact.
- **Where:** `SensorOverlaps` (`engine/include/physics/SensorOverlaps.h`) and the
  cast in `engine/src/physics/SensorShapeQuery.cpp`, which knows the
  displacement that produced an Enter.
- **Trigger:** the first content needing a one-way trigger. The direction
  belongs on the sensor occurrence, as a vector field of the `sensor.*`
  events' payload, which a response then reads as a port.

## Response inspector: query sources and picking entities

- **What:** a sequence argument may be a query's answer (or one member of it),
  and an entity named by identity; both run and are tested. The inspector
  offers constants, ports and a typed identity, but no query source and no
  picking an entity in the view.
- **Where:** `editor/common/src/vocabulary/VerbSequenceRows.cpp` (the argument
  sources) and `editor/level_editor/src/authoring/ResponsesEditorAdapter.cpp`.
- **Trigger:** a designer authoring a query-driven argument or pointing a
  command at another placed entity -- a switch that opens a remote door is the
  first -- or the owner's walkthrough of response authoring.

## Turning a trigger's direction in the view

- **What:** a trigger draws an arrow for every direction its commands are given;
  the direction is edited in the inspector, not by dragging the arrow.
- **Where:** `ResponsesEditorAdapter::BuildViewport`; a dial or handle would
  commit through the same `SetVerbSequenceValue` step.
- **Trigger:** the owner's walkthrough of trigger authoring, or a designer
  asking to aim a bumper in the view.

## Retiring verb relays

- **What:** a relay -- native code activating one binding a placed entity names
  in a shared `authored.bindings` asset -- is what a response does when native
  code delivers a game-declared event at the entity, plus conditions and
  policy. Its only producer is the arena template's `award` console command.
- **Where:** `engine/include/logic/VerbRelay*.h`, `engine/src/logic/VerbRelay*.cpp`,
  `templates/arena/src/ArenaGame.cpp` and `templates/arena/assets/levels/arena_room.sscene`.
- **Trigger:** the arena's award migrated to an `arena.award_requested` event
  and a response on its score entity; then remove relays, their serializer and
  `logic.relay.*`.

## Skipping the encode of an unheard publish, and source-indexed subscribers

- **What:** `AuthoredEventDispatcher::Publish` encodes, validates and queues
  every occurrence whether or not anything subscribes, and delivery scans every
  subscriber of the event to filter by source (`MatchingSubscribers`). Targeted
  responses never go through it, so no frequent producer publishes today.
- **Where:** `engine/src/authored/AuthoredEventDispatcher.cpp`, `Enqueue` and
  `MatchingSubscribers`; `test/runtime/ResponsesDeliveryBench.cpp` measures
  publish and drain beside delivery.
- **Trigger:** LevelFlow, the first consumer with many source-filtered
  subscriptions, or a producer that publishes a per-contact event globally.

## Marking an event's instigator by annotation

- **What:** `AuthoredEventDefinition::Instigator` names the payload field a
  response's commands carry as their instigator. Events declared by hand set
  it; `SENCHA_EVENT` has no annotation for it, so a generated event names none.
- **Where:** the annotations in `engine/include/authored/AuthoredAnnotations.h`
  and `sencha-component-codegen`.
- **Trigger:** the first event declared through the generated API that a
  response's cooldown or a verb's provenance needs to attribute.

## Captured payload size

- **What:** an admitted response captures its ports as `AuthoredValue`s, which
  carry a string and a vector beside the scalar, so a three-value payload and
  its entry measure 344 bytes (`ResponsesDeliveryBench`: 10,000 waiting
  responses, 3,440,000 bytes). Warmed, nothing allocates.
- **Where:** `ResponsesSystem::Activation` and `ResponsesSystem::Values`
  (`engine/include/logic/ResponsesSystem.h`).
- **Trigger:** a producer admitting thousands of responses a tick, or a memory
  budget pass over the simulation.

## Inventory contents follow their container through partition followers

- **What:** inventory moves a container's contents into the container's
  partition itself (`MoveContents` in `engine/src/inventory/Inventory.cpp`), and
  only when inventory places the container. A container moved by anyone else
  (`RuntimeWorld::MoveEntityToPersistent`) leaves its contents behind.
- **Where:** `ecs/PartitionFollowers.h` is the general mechanism; inventory lives
  on `claude/composition`, which `main` does not have yet.
- **Trigger:** `claude/composition` merging. Register inventory's index as a
  partition follower and delete `MoveContents`.

## Predicting a firer's own projectile

- **What:** a client sees its own projectile only when the authority's arrives,
  a round trip plus the interpolation delay after the input. Nothing guesses a
  launch.
- **Where:** `ProjectileSystem::Launch` (authority only) and `Present`; the
  prediction pattern to follow is `PredictionJournal` beside
  `docs/gameplay/abilitykit.md`, "Prediction".
- **Trigger:** a game shipping projectile combat at internet latency. Guess the
  launch in the predicted ability pass as a local, unreplicated copy of the
  prefab flown on the command timeline; match it to the authority's projectile
  by instigator and launch tick when that arrives, and retire the guess. The
  guess strikes nothing.

## Hit regions that follow animation

- **What:** a `HitRegion` sits where its entity stands, so a character's parts
  follow its capsule and turn but not its pose. A region anchored to a joint
  needs the pose on the authority, and `AnimPoseSystem` runs only where a pose
  is presented (`AnimationHost::PresentsPose`).
- **Where:** `physics/components/HitRegion.h`, `HitRegionResolver`; poses in
  `anim/AnimPosePool.h`.
- **Trigger:** the first animated character whose parts must move with its
  pose, such as a crouch or a reaching arm a shot should miss. That consumer
  decides whether a headless authority poses characters, and adds a socket to
  `HitRegion` resolved through a simulation-side joint readback.

## Effect reactions on a remote client

- **What:** `EffectReactionSystem` runs on the authority. The shake it starts
  is presented only by the authority's own views, so a struck remote player's
  screen does not shake, and its push reaches a predicting client only as a
  correction, which may snap.
- **Where:** `logic/EffectReactionSystem.h`; cues reach clients through
  `AbilityCueLog`; prediction corrections in `docs/gameplay/responses.md`.
- **Trigger:** the first networked session where a remote player is struck.
  It sends the shake as a cue to the struck player's machine and measures
  whether a shove snaps there, carrying it in the predicted motion if it does.

## An event for each effect application

- **What:** content cannot answer an effect landing through `responses`; only
  `EffectReactionSystem` reads the application log.
- **Where:** `effects/EffectApplicationLog.h`.
- **Trigger:** the first content that must answer an effect rather than a
  strike, such as a sound for a heal. It declares an `effect.applied` event and
  decides which attribute's change its payload carries.

## Lag-compensated projectile contacts

- **What:** a projectile's contacts are decided against the authority's present.
  A target the firer saw at an interpolated, older pose is struck where the
  authority has it now.
- **Where:** `ProjectileSystem::Sweep`.
- **Trigger:** the same as hitscan lag compensation in `docs/plans/networking.md`:
  combat at internet latency where players notice. It needs a per-tick history of
  character capsules and body poses to sweep against.

## Bouncing and homing projectiles

- **What:** a contact strikes, passes through or stops a projectile; nothing
  reflects it or steers it.
- **Where:** `ProjectileSystem::Fly`.
- **Trigger:** a grenade that should skip off walls, or a seeker. A bounce is a
  third contact outcome (reflect the velocity about the normal, scaled by a
  restitution, while bounces remain) that strikes nothing; homing turns the
  velocity toward a target entity by a rate.

## Projectile poses on the wire every snapshot

- **What:** a flying projectile's `LocalTransform` changes every tick, so it is
  sent in every snapshot, though its path between contacts is fully described by
  its launch state.
- **Where:** `ProjectileFlight` and the replication change store.
- **Trigger:** projectile counts where snapshot budget is the limit. Replicate the
  segment (origin, velocity, start tick, gravity) once per contact and evaluate
  the pose on the receiver, with the root's transform left out of replication
  for those entities.

## Attachment placement over visible partitions only

- **What:** `PlaceAttachments` walks every attachment in the World each frame.
  Only the destroy drain and the holder check need the whole World; placement
  itself is needed only where a zone is visible. An unposed root also rebuilds
  its bind pose once per attachment per frame.
- **Where:** `PlaceAttachments` in `engine/src/attach/Attachment.cpp`, called from
  `FramePhase::ExtractRender` in `engine/src/app/EngineFramePhases.cpp`.
- **Trigger:** a level with many dormant zones whose attachments show in a
  frame profile, or a measured placement pass over the 0.25 ms budget.

## One holder-death rule for lease providers

- **What:** each lease provider ends what a dead holder held its own way:
  `AudioOperations::Sweep`, the movement and effect holds, and attachments
  through `PlaceAttachments`. A shared holds table could own that rule once.
- **Where:** `engine/src/audio/AudioVerbs.cpp`, `engine/src/movement/MovementVerbs.cpp`,
  `engine/src/effects/`, `engine/src/attach/`.
- **Trigger:** a fourth lease provider that needs to end holds when its holder
  dies. Presentation contributions (mesh overrides, outlines, post effects) end
  theirs through one rule, `PresentationContributions::SweepDeadHolders`, run by
  `PresentationLifetimeSystem`; they are cosmetic and local, so they do not count
  toward this trigger for the simulation's leases.

## Scene depth in post effects

- **What:** `post_effect.glsli` declares `depthIndex`, which stays `0xFFFFFFFF`.
  `PostEffectDesc` has no declared inputs, since every input is in the fixed push block.
- **Where:** `engine/src/graphics/vulkan/PostEffectChain.cpp`,
  `engine/include/graphics/vulkan/VulkanDepthTarget.h`.
- **Impact:** an effect cannot read depth, so no depth of field or fog.
- **Trigger:** the first effect that samples depth. Give the presentation's depth
  `SAMPLED` usage, a depth-aspect view registered through the descriptor cache, and a
  barrier into a read layout around the chain.

## Authored post effects

- **What:** a transient post effect's weight is driven from C++ every frame, on whatever clock
  its writer picks, and `PostEffectContribution` carries its own `Held`/`Session` lifetime and
  `RemainingWallSeconds`. Camera shakes already have an authored envelope and cosmetic verbs
  (`CameraEffectTiming`, `CameraShakeVerbs`); post effects have neither. A standing look, such
  as a level's bloom, is authored already, as [presentation settings](renderer/presentation-settings.md);
  this is about effects that come and go.
- **Where:** `engine/include/render/post/PostEffectContributions.h`,
  `engine/src/camera/CameraShakeVerbs.cpp`.
- **Impact:** a fade or a hit flash is C++ in the game, not data a designer tunes.
- **Trigger:** a second game effect that wants an envelope rather than a weight its own
  code drives. Give the contribution an envelope (attack, hold, release on wall or
  simulation time) and add a cosmetic verb, sharing the camera effects' timing where the
  clocks agree.

## Culling skinned meshes by their posed bounds

- **What:** extraction culls a skinned mesh by its rest bounds moved to the entity
  (`RenderExtractionSystem.cpp`), while outlines take `PosedWorldBounds`, because a posed
  limb reaches outside the rest bounds. One entity has two bounds in one extraction.
- **Where:** `engine/src/render/extract/RenderExtractionSystem.cpp`,
  `engine/include/render/SkinnedPoseFrameData.h`.
- **Impact:** a mesh posed far from its rest shape, such as an arm thrown out, can be culled
  while part of it is on screen.
- **Trigger:** a skinned mesh seen popping at a screen edge. Culling by posed bounds means
  posing before culling, which costs pose work for meshes that would have been culled.

## A scale on a lasting effect's duration

- **What:** a lasting effect's length is its definition's `seconds`, with nothing
  at the application to lengthen or shorten it. `scale` multiplies magnitudes only.
- **Where:** `ApplyEffect` (`engine/src/effects/EffectSystem.cpp`), `effect.apply`
  (`engine/src/effects/EffectVerbs.cpp`).
- **Trigger:** a game choosing a length by difficulty, which Loss Function plans
  for its seizure effect (`assets/effects/prayer_seizure.sdata`). Add an optional
  `duration_scale` to `effect.apply`, default 1, without touching `effect.hold`.

## Contact lines on other machines

- **What:** a contact line lives on the authority only, so a client neither draws
  one nor knows its tip.
- **Where:** `engine/src/line/ContactLineSystem.cpp`.
- **Trigger:** the first game shipping a contact line in a networked session.
  Replicate the line's root as a prefab, as `ProjectileSystem` does, with its stage
  and instigator as a wire component like `ProjectileFlight`.

## Cook-time generation of anchored navigation links

- **What:** anchored links are hand-authored. The cook could sample walkable
  borders, pair them within rope range and validate clearance, emitting the same
  `NavLinkRecord`s.
- **Where:** `engine/src/assets/cook/NavigationCook.cpp`, `navigation/tooling/NavMeshSnapshot.h`.
- **Trigger:** the first level needing more anchored links than is reasonable to
  place by hand. Nothing at runtime changes: routes already read the records.

## Culling a posed mesh by its pose

- **What:** skinned meshes frustum-cull against their rest bounds, so a limb posed outside those bounds can pop out at the edge of the view.
- **Where:** `RenderExtractionSystem::EmitSkinnedMeshes`; `PosedWorldBounds` already computes the tighter bound per pose.
- **Trigger:** a report of popping on an animated enemy. A cooked per-skeleton reach would change the mesh format, so it needs format treatment.

## Smoothing the view over a step

- **What:** the character mover climbs a ledge up to `StepHeight` (0.35 m) in
  one tick, and a first-person camera parented to the body rises with it, so the
  view pops up a stair rather than gliding.
- **Where:** `engine/src/physics/CharacterMover.cpp` decides the step and reports
  nothing about it; a presented camera effect beside `CameraBob`
  (`camera/CameraBobState.h`) would ease the view back down over the step.
- **Trigger:** content with stairs or ledges a player walks up in first person.
  That needs the mover to report the height it stepped on the tick it stepped
  (a neutral component like `StrideCycle`), and the camera effect to read it.

## Strides of characters a machine does not simulate

- **What:** `StrideCycle` counts from `KinematicState::Velocity`, which is
  owner-only on the wire, so on a client every other player's stride stands
  still: their rigs placed by `StrideCycle`, and any camera following them,
  would not step.
- **Where:** `StrideCycleSystem` (`engine/src/movement/StrideCycleSystem.cpp`).
- **Trigger:** a spectator or third-person camera on a remote pawn, or a remote
  rig placed by its stride. Counting from the presented position's motion over
  the ground, rather than from velocity, serves both without sending anything.

## Movement occurrences: a landing and what answers it

- **What:** detecting a landing once, with its impact, and letting gameplay,
  presentation, animation and other machines answer it. Nothing detects one
  today; Loss Function tracks landings twice in game code
  (`MovementNoiseState`, and Vector's descent landing count).
- **Findings**, each checked against the tree:
  - *Detection needs no stored state.* `CharacterMoverPool::Sweep` is the one
    place that overwrites `SupportState` and `KinematicState`, and it holds both
    the old and the new values. Arriving on stable ground, and the velocity the
    contact took away, are a pure function there. The same comparison gives
    leaving the ground, starting or ending a slide (`Steep`), and striking a
    wall (velocity lost along a contact normal). A step up is not in
    `CharacterMoveResult`; that needs the backend to report it.
  - *Prediction replays the mover.* `Sweep` runs live (`Drive`, and
    `StepScaledCharacterTick` for dilated characters) and again in
    `PawnStateReplay`, which re-runs every unanswered tick after every snapshot.
    Detection inside it would fire once per replay. So the step returns what it
    detected and its caller decides: live callers forward it, the replay drops
    it. Both components are restored from the snapshot before a replay, so
    detection stays a function of agreed state. A landing that replay moves to
    another tick is not presented again; one only replay finds is never
    presented on the owner, whose own view never showed it.
  - *The channel already exists.* Abilitykit's rule D-F says transient
    notifications are a per-tick record a system pulls. `EffectApplicationLog`
    (written as effects land, read by `EffectReactionSystem` in fixed logic,
    cleared once a tick) and `ProjectileStops` are that shape. A per-tick
    contact log written by the live callers needs no new event framework.
  - *Gameplay and presentation are adapters, not sources.* Authored events
    (`AuthoredEventDispatcher`) carry content rules, and responses run only on
    the authority; cues carry what viewers see. Both would read the one log,
    and the mover knows neither. This matches the deferred effect-application
    event and remote reactions below.
  - *Presentation needs a tick-to-frame bridge.* A per-tick log is cleared each
    tick and a frame can run several, so a presentation consumer runs per tick:
    it starts tick-timed effects, as `EffectReactionSystem` starts shakes, or it
    appends a cue.
  - *Cues are exactly-once only within bounds* (`AbilityPresentationSystem.cpp`).
    A ring of `kAbilityCueLogCapacity` (8) loses entries on overrun, counted in
    `Overwritten`. First sight presents only entries younger than
    `kAbilityCueWindowTicks` (30). A predicted cue is matched to the authority's
    by activation, op, tick, target and ordinal: a guess a tick off presents
    twice, a refused guess has already been presented, and 16 guesses are
    remembered. Abilities escape replay duplication because the ability pass is
    not re-run in reconciliation (`AbilityPredictionJournal` restores guesses).
    `AppendSourcedCue` has no predicted path; projectiles and lines do not
    predict.
  - *Remote machines cannot detect landings themselves.* A pawn they only
    receive arrives without support or velocity (owner-only, see "Locomotion
    facts for a pawn this machine only receives"). Either the authority sends
    the landings a game chooses as cues, or that entry's decision sends support
    to everyone, which still carries no fall speed.
  - *Animation has its own channels.* A body's land-then-idle must stay on facts
    (`Grounded` with an `edge` derivation) so a late joiner reconstructs it. A
    cosmetic reaction rides a cue through `anim.present`. An authoritative one
    is an Impulse request, which replicates and is observed once. The CTX
    rifle's in-hand landing dip is a spring on vertical speed and needs no
    occurrence at all.
  - *Costs* (sizes measured against this build): `AbilityCueLog` 584 B and
    `AbilityPresentationState` 624 B on every actor with `ActiveAbilities`,
    `AbilityPredictedCues` 584 B on a predicting pawn. A landing cue adds no
    memory where the log already is. A character without abilities would need
    about 1.3 KB to carry cues, too much for a crowd. On the wire one new entry
    sends three u64, three u32, a 48-bit point and a 24-bit normal, about 45
    bytes per peer before compression; not measured. A per-tick log costs
    nothing per entity.
- **Recommendation:** `Sweep` returns contact changes (landed, with the velocity
  lost, point, normal and surface; left the ground; slide began or ended; wall
  struck) as a value. Live callers append them to a per-tick log; the replay
  drops them. Games read the log and decide what a landing means: Loss
  Function's movement noise reads it in place of its own tracking, a game's
  thresholds pick which landings are hard and which cue to append, and the
  sound, dust or view dip are cue presentations in data.
- **Unresolved:**
  - a landing's strength on a cue: `AbilityCueEntry` has no magnitude, and
    adding one costs every cue; separate cues per severity keep the thresholds
    in game data, which is preferred;
  - the owner's own landing: presented at once from its live log, the
    authority's cue must not present it again. Either the owner records it in
    `AbilityPredictedCues` under the authority's occurrence identity (correct
    only when the ticks agree), or the presenter skips replicated movement cues
    for the entity it predicts;
  - the cue log lives in `abilities/` and only on actors with abilities;
  - a step up and a wall strike need the mover backend to report them.
- **Trigger:** the first landing sound, fall damage, or a third landing tracker
  in game code.

## Editor shell windows following their window's view

- **What:** Kyusu's shell windows size their three surfaces by hand every frame
  instead of following their window (`UiService::CreateWindowSurface` and
  `SetWindowView`). They lay out at a scale of the editor's choosing
  (`ChooseUiScale` over the user's UI scale preference), not the window's
  display scale, and the engine already publishes the primary window's view
  with the display scale, so two owners would publish one window's view.
- **Where:** `ShellWindow::Update` in `editor/common/src/shell/ShellWindow.cpp`;
  `Engine::PublishWindowView`; `docs/ui/architecture.md` §13.
- **Trigger:** a UI scale setting the engine's window view carries (a player's
  interface scale on the options page), at which point the editor sets that
  preference instead of scaling its own surfaces, publishes views only for the
  windows it owns, and creates its surfaces as following ones.

## Changing level inside a network session

- **What:** `LevelChange::Request` refuses while a session runs, because a peer
  is told which map to load only when it is admitted (`NetSession`'s announced
  map) and would stay on the old level.
- **Where:** `engine/src/app/LevelChange.cpp`; the announcement in
  `engine/src/app/EngineFramePhases.cpp`.
- **Trigger:** a co-op game that changes level, rests or restores a save with a
  second player connected. Then announce level changes to admitted peers and
  release and request their bodies through the same change.

## Saving what was created at run time

- **What:** a save keeps only how authored entities differ (destroyed, and the
  component state their owners register). A body's attributes and grants,
  items, and anything created, dropped or deposited at run time are not
  saved, and a runtime entity in a zone is destroyed unrecorded when the zone
  unloads.
- **Where:** `save/SavedGames.cpp` (`Write`), `zone/ZoneStateStore.h`.
- **Trigger:** saving a body's progression or an inventory, which needs a body
  saved as a patch against its prefab. The runtime persistent id mint exists
  (`world/identity/PersistentIdMint.h`), but its counter (`PersistentIdMint`)
  is World state no save keeps: the first saved runtime entity saves the
  counter with it, or a restored game mints ids it already holds.

## Unknown save chunks are dropped on rewrite

- **What:** `DecodeSavedGame` skips a chunk it does not know, and the next
  write does not carry it. Unknown sections are kept; unknown chunks are not.
- **Where:** `engine/src/save/SavedGame.cpp`.
- **Trigger:** a second chunk kind added after format 1. Then either fold new
  data into sections or keep unknown chunks' bytes in `SavedGame`.

## Replication change store copies every live entity each publish

- **What:** `ReplicationChangeStore::Update` copies every live `EntityState`,
  with its per-component byte and generation vectors, into `LiveEntities` on
  every publish, then sorts it. Measured on the profile preset, before the
  composition port: about five allocations and 0.18 us per replicated entity
  per publish, whether or not anything changed
  (`docs/plans/evidence/entry-entities/results.md`; `scripts/bench_replication.sh`
  measures it again).
- **Where:** `engine/src/net/ReplicationChangeStore.cpp` (`Update`, the
  `LiveEntities` rebuild), `engine/include/net/ReplicationChangeStore.h`.
- **Trigger:** a measured publish cost or allocation budget that replicated
  entity counts exceed: a scene, or inventory replication under the D3
  relevance filter, whose publish no longer fits its frame budget. It is not a
  dependency of definition composition or of inventory.

## Composing data definitions on the staging worker

- **What:** a data definition that extends or includes others composes when it
  commits, on the owner thread. That commit reads and parses each source file
  synchronously, then composes, validates and compiles.
  - Measured on the fixture content before the composition port, 500 runs
    each:
    - `hero.facts.sdata` commits in about 180 µs and `examples.facts.sdata` in
      about 85 µs, each with one source;
    - a plain definition commits in about 3 µs.
  - Every commit reads its sources again; nothing is cached across commits.
- **Where:**
  - `DataAssetLoader::ComposePending` (`engine/src/assets/data/DataAssetLoader.cpp`).
  - The worker cannot do this today. `IAssetStager::LoadStaged` receives only
    the id-to-path snapshot, and nothing maps a source's asset path to its file.
  - Moving the work needs a path-to-file snapshot beside `AssetPathsById`,
    which changes the stager contract every kind implements.
- **Trigger:** a content set or preload in which composition costs more than
  1 ms of owner-thread time in one drain. At today's figures that is roughly
  ten composing definitions committing together. Ability, effect and cue
  variants authored as content (`CompositionGate` tests the shapes) are the
  first consumer to measure.

## Authoring an ability variant in the Ability Workbench

- **What:** the Ability Workbench reads and edits a definition's own data, so
  it does not take an ability that extends or includes another
  (`AbilityWorkbench::Ability`).
  - Kyusu shows such a variant in the generic document sheet, as it composes
    and with provenance.
  - Op arguments are not described by the ability schema, so that sheet has no
    row for a variant's patched argument.
  - The variant has no strip, element views or live preview.
- **Where:** `editor/ability_editor/src/authoring/AbilityWorkbench.cpp` and
  its element, argument and preview views, all of which read `AbilityData()`.
  The composed value and provenance are on `DataDocument` (`ComposedData`,
  `CompositionResult`), and sparse writes are in `data/DataPatch`.
- **Trigger:** the first ability variant authored as content, or the
  AbilityKit work that brings variants to the workbench.

## A fragment included onto a prefab's own entity

- **What:** a fragment may go only onto an entity the prefab inherits.
  Aimed at one of the prefab's own entities, it is refused with
  `fragment_target`.
  - Supporting it means saving that entity as the difference from what the
    fragment supplies, not whole, which the local save path does not do.
  - Until then, a prefab that wants a fragment on its own root extends a base
    that owns the root.
- **Where:**
  - the resolver: `ComposeLayers` in
    `editor/common/src/scene_source/SceneComposition.cpp`;
  - the local save: `EditorDocument::BuildSceneSource`.
- **Trigger:** the first prefab that includes a fragment without extending a
  base.

## Kyusu authoring for prefab extends

- **What:** Kyusu loads, edits in place, saves and cooks a prefab that extends
  another, but has no surface for the composition itself:
  - choosing what a scene extends and includes, and whether it is abstract,
    from its card (`SceneEnvelopeProperties`, an envelope command);
  - saying in the component inspector where an inherited value came from
    (the base or a fragment), from the resolver's provenance, which the
    projection does not keep yet;
  - "Revert to inherited" wording on the existing reset verbs, and Restore for
    an inherited component removed here;
  - the prefab explorer still lists abstract prefabs (placing one is refused).
  Until then these members are written by hand, and Kyusu keeps them as read.
- **Where:** `editor/level_editor/src/authoring/ComponentPropertySource.cpp`,
  `SceneInstanceProjection` (provenance per element), `EditorDocument` (an
  envelope setter and command), `SelectionSummary`; ported from
  `claude/composition` 9ef2aa44 and a292c5f5.
- **Trigger:** Kyusu authoring for prefab extends: the first designer who
  builds a prefab chain in Kyusu rather than by hand.

## A response binding naming another member of its prefab

- **What:** a response binding's `entity` argument names an entity by persistent
  id and resolves through `PersistentEntityIndex` when it runs. It does not
  follow a copy:
  - A spawn strips authored identity (`SmapPackageOptions::StripAuthoredIdentity`),
    so a spawned member carries no id and a binding naming it finds nothing.
    `PersistableRef` fields resolve between a spawn's members at import
    (`SceneSerializationContext::PackageMembers`); a binding's argument does
    not, because `ResponsesSerializer` keeps it as authored text.
  - A placement renames the `DataFieldKind::Entity` fields a component's
    `CompositionSchema` declares. A binding's argument is not one: bindings are
    keyed by authored keys, which a record schema cannot describe, so a placed
    prefab's binding still names the source's id.
- **Where:** `engine/src/logic/ResponsesSerializer.cpp`,
  `RenameMemberReferences` in
  `editor/common/src/scene_source/SceneComposition.cpp`.
- **Trigger:** the first prefab whose response binding names another of its
  own members.

## Prefab harvest pins and keyed-list order

- **What:** Kyusu rebuilds a derived prefab's patches from a diff of live state
  against the composed base. A value pinned equal to the base's (so a later
  base change does not reach it) is dropped, and so is the order of a keyed
  list. Data definitions keep both, because their patches are edited, not
  diffed.
- **Where:** `SceneInstanceProjection::Harvest` and `DiffSceneComponents` in
  `editor/common/src/scene_source/SceneComponentDiff.cpp`.
- **Trigger:** the first prefab that must keep a value its base later changes,
  or a keyed list whose order is meaningful (a priority list) on a derived
  prefab.

## Recomposing resident children while their base is edited

- **What:** while a base prefab is edited in Kyusu, children of it that are
  resident but not open keep their old composition until the base is saved.
- **Where:** `SceneSourceCache` invalidation and `SceneInstanceProjection`
  rebuilds in `editor/level_editor/src/document/`.
- **Trigger:** a live-preview workflow in which a base and its placed children
  are on screen together while the base is edited.

## Ability Workbench rows for cost provider arguments

- **What:** a cost group's `resources` show their provider and arguments as
  plain data. An op's arguments are laid out from the verb's or query's contract
  (`editor/ability_editor/src/authoring/AbilityArguments.cpp`); a resource's are
  not.
- **Where:** `AbilityArgumentProperties` reads `AbilityOpContract`, which knows
  ops only.
- **Trigger:** the first cost provider content authors use, which is
  inventory's item provider (composition Stage 4).
- **Consumer:** item-cost abilities (an arrow per shot, a bottle per drink).
