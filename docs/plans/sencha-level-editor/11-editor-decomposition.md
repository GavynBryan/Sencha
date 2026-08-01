# Kyusu Editor Decomposition Plan

Status: executed. Phases 0-4 landed, along with the parts of Phase 5 that earned
their own owner (`EditorCookRuntime`, the document verbs and read models moved
off the composition root). The remaining Phase 5 groups -- input, viewport, and
UI runtimes -- were deliberately not built: the three couplings this plan already
names across those boundaries make them one bring-up sequence rather than three
owners, so grouping them would add indirection without moving an invariant.

Work beyond this plan landed with it, from an audit of the result: the pending
edit scope gained a single owner, document replacement now cancels transactions
before destroying the documents they staged into, saves and cooks resolve open
previews, tools own their settings and their chrome, and the authoring half of
kyusu became a library the test targets link.

This plan is intentionally editor-only. Runtime and ECS stabilization landed on main with the unified runtime container; none of that work belongs here.

## Goal

Keep `EditorWorkspace` as the per-document authoring composition boundary while removing the editing mechanisms and state machines that have accumulated inside it. Keep `EditorServices` as the application composition root while grouping its concrete lifetime domains.

This is not a mandate to replace concrete ownership with interfaces. The target is clearer ownership, smaller mutation surfaces, and tests around the mechanisms that move.

## Non-goals

- Do not remove `EditorWorkspace` or turn it into an abstract service locator.
- Do not add interfaces where there is only one implementation.
- Do not split files without moving state and responsibility.
- Do not reorganize the brush kernel, render passes, or generic editor shell merely for symmetry.
- Do not mix runtime `World` or ECS work into this branch.

## Existing boundary to preserve

The editor remains layered as:

1. Engine
2. Core abstractions and brush kernel
3. Authoring subsystems
4. `EditorWorkspace` as the per-document aggregator
5. `EditorServices` as the application composition root

The problem is not that these upper-level owners exist. The problem is that they increasingly implement the mechanisms they should compose.

## Measured starting point

`EditorWorkspace` is 283 header lines and 1211 implementation lines. `EditorServices` is 172 and 1055, holding 33 data members. Nothing named in this plan has been extracted: `editor/kyusu/src/workspace/` contains only `EditorWorkspace`, `BrushManipulationSink`, and `EscapePolicy.h`, and `editor/kyusu/src/app/` contains only `EditorApp`, `EditorServices`, and `EditorFrameHook`.

Two concerns present in those files are newer than the rest of this plan and are folded into the phases below: the component-affordance and world-dock authoring layer in `EditorWorkspace` (Phase 2), and the cook session in `EditorServices` (Phase 5).

`EditorWorkspace.cpp` is compiled into no test target. The headless `editor_tests` target exists and is GUI-free by construction, so the mechanisms extracted here become testable exactly when they stop depending on panel and render state.

## Phase 0: Characterization

The geometry and math kernels under these mechanisms are already covered and should not be restated: `test/brush/BrushOpsTests.cpp` (bridge path pairing and reversal, inset shells, bevel profiles), `test/brush/MeshEditServiceTests.cpp` (the bridge verb at service level), and `test/editor/GridFrameTests.cpp` (frame derivation from a face, in-plane rotation, snapping on a rotated lattice).

The gap is the state-machine layer above them, which no test reaches today. Before moving state, add focused tests for:

- pending bridge begin, regenerate, commit, and cancel
- pending inset and bevel begin, parameter update, commit, and cancel
- cancellation when the edited entity or document disappears
- the pending-edit command-stack scope opening and closing with the pending state
- interaction rebuild after document changes
- grid origin, face alignment, rotation, and reset
- material projection copy, paste, and apply
- repeat-last-action behavior

Prefer behavior tests around the extracted mechanism. Avoid broad UI snapshots.

These tests can only join `editor_tests` if the extracted mechanism compiles without ImGui or render state. Treat that as a constraint on how the extraction is shaped, not as a reason to skip the test.

## Phase 1: Extract pending editing state machines

### `PendingBridgeEdit`

Own:

- selected edge-path capture
- bridge path specifications
- preview mesh generation and rebasing
- preview entity lifetime
- segment changes and regeneration
- commit and cancel behavior
- captured scene, document, and selection state

`EditorWorkspace` should expose a small coordinating surface such as:

```cpp
BridgeEdit.Begin(selection, scene, document);
BridgeEdit.SetSegments(segments);
BridgeEdit.Commit(commands, selection);
BridgeEdit.Cancel();
```

The mechanism owns its state. `EditorWorkspace` must not retain a parallel enum and data bundle.

### `PendingElementEdit`

Own:

- inset and bevel captures
- original meshes
- face and edge selections
- width, distance, and segment parameters
- preview regeneration
- commit and cancel behavior
- restoration of original meshes

Use one concrete state machine because inset and bevel share capture, preview, commit, and cancel semantics. Do not create a hierarchy of edit-operation objects unless a third operation proves the switch is genuinely unstable.

## Phase 2: Extract interaction composition

Create a concrete `WorkspaceInteractionRuntime` that owns and wires:

- `BrushManipulationSink`
- `ToolContext`
- `ToolRegistry`
- `ViewportToolDispatcher`
- `EditSessionHost`
- manipulator sessions
- tool activation callbacks
- capability queries
- interaction reset and cancellation
- `EditorAffordanceService` and `EditorEntityRecipeRegistry`
- the picking entity-proxy provider
- the duplicate-snapshot remap callback shared by `BrushManipulationSink` and `DuplicateEntitiesCommand`
- the pivot, mode, and zone selection observers

`EditorWorkspace` supplies the active document/session inputs and requests rebuilds. It should not individually own every node in this graph.

The affordance layer is the reason this phase grew: affordances now feed picking, decide whether the manipulator has edit targets, gate whether scale is allowed, and contribute viewport overlay labels. Those are four call sites reaching into one service from four different places in the workspace, which is what the interaction runtime is for. The recipe registry travels with it because affordance adapters and creation recipes are registered together at the same point in bring-up.

The extracted object remains in the workspace layer because it composes authoring subsystems. It must not move downward into the generic tool or interaction libraries.

## Phase 3: Extract focused editing concerns

### `GridEditing`

Own:

- `GridState`
- set origin from selection
- align to selected face
- rotate in plane
- reset frame
- synchronize orthographic view axes

The frame math already lives in `editor/kyusu/src/viewport/GridFrame.h` and `GridSettings.h`. This extraction moves the workspace verbs that derive a frame from the current selection and push it at the orthographic views, not the math underneath them.

### `MaterialProjectionEditing`

Own:

- active material state
- UV projection clipboard
- apply material to selected faces
- copy projection
- paste projection

The state type and the edit primitives already live in `editor/kyusu/src/meshedit/ActiveMaterialState.h` and `FaceMaterialEdits`. What remains in `EditorWorkspace` is the clipboard member and three thin verbs over it, which makes this the smallest move in the plan and a reasonable first one if a warm-up is wanted before the pending-edit machines.

### Selection-derived actions

Keep duplicate, merge, separate, delete, select-all, and origin actions as focused concrete functions or one small action object. Do not create a universal `WorkspaceCommandService`; that would merely rename the junk drawer.

## Phase 4: Narrow `EditorWorkspace`

After the extractions, `EditorWorkspace` should own:

- active document and world-document routing
- selection
- interaction runtime
- pending bridge and element edits
- grid editing
- material projection editing
- pivot, overlay, and view state
- coordination between the above

Success criteria:

- adding a pending edit does not add an enum and several fields to `EditorWorkspace`
- a panel receives only the mechanism or read model it uses
- document switching has one explicit cancellation/reset path
- pending edit state cannot survive the session it references
- tool registration does not require unrelated workspace mutation

## Phase 5: Decompose `EditorServices`

Only after `EditorWorkspace` settles, group the composition root into concrete lifetime domains.

### `ProjectSession`

Own:

- project descriptor and content mounts
- loaded game module
- runtime assets
- material library
- source watcher and hot reload

### `EditorInputRuntime`

Own:

- input router
- viewport navigation
- shortcut registry and keymap loading
- pointer capture integration

### `EditorViewportRuntime`

Own:

- editor render feature
- viewport camera system
- frame hook
- renderer registration
- viewport target and scene-resource lifetime

### `EditorUiRuntime`

Own:

- UI feature
- panels and chrome
- toolbar, sidebar, and status bar
- thumbnails
- panel registration

### `EditorCookRuntime`

Own:

- the cook session and its profile selection
- the play-in-editor driver
- the applied and preview cook serials
- the reconciliation that republishes cooked world and level paths into the driver
- the baked-lighting preview refresh

This is the largest cluster on the composition root with no owner of its own: `CookSession`, `PieDriver`, the `CookProfilesPanel` pointer, `SelectedCookProfileId`, `AppliedCookSerial`, and `PreviewCookSerial` all sit as raw members, and the per-frame reconciliation between them runs inline in the frame path. The launched player belongs in the same group as the cook because it consumes cook output: the serials exist precisely to hand a freshly published cook to the driver. Grouping them lets the toolbar's cook callbacks bind to one object instead of eight lambdas reaching into `EditorServices`.

### Verbs that do not belong in the composition root

`BakeSelectedBrushes`, `RevertSelectedBakedBrushes`, `SelectionHasBakedBrush`, and `ExportSelectionGlb` are document-editing operations implemented on `EditorServices` and reachable only through the tool properties panel. Move them into the workspace layer beside the other selection-derived actions from Phase 3.

`BuildUi` is 313 lines and also carries inline component walks that count baked-contribution lights and authored irradiance volumes to feed panel readouts. Those are read models over the document. Name them and move them out of the UI construction path rather than adding more lambdas there.

### Ownership that stays at the root

`EditorServices` remains the sole top-level owner and explicit bring-up/teardown sequence. `CommandStack`, `EditorWorkspace`, and `DocumentFileActions` stay directly root-owned: they are the document and undo hub that every group reads, and pushing them into one group would force the others to reach through it. Do not add `IProjectSession`, `IEditorRuntime`, factories, or a service locator.

Three couplings cross the group boundaries and must be preserved deliberately rather than discovered mid-move: the input guard reads viewport panel hover state, UI construction reads the render feature for viewport targets and shadow readouts, and `MaterialThumbnailCache` teardown is ordered against both the render feature and runtime assets. That teardown contract currently survives as a comment on the member; after grouping it must be an explicit sequence.

## Dependency enforcement

`scripts/check_editor_layering.sh` already enforces three include-direction rules and runs as the ctest `editor_layering_directions`. Its rule C hardcodes that only `editor/kyusu/src/app/` and `editor/kyusu/src/workspace/` may include `workspace/` headers, filtering on the including file's path. Mechanisms extracted into those two directories need no script change. A new directory requires extending that alternation as a deliberate edit, not as a fix applied after the check goes red.

Extend the checks only when an extracted boundary can be stated mechanically. Useful rules include:

- pending edit mechanisms may depend on document, selection, mesh-edit, and command abstractions, but not UI panels
- panels may read narrow workspace mechanisms, but authoring mechanisms may not include panel headers
- lower authoring subsystems may not include `EditorWorkspace`
- only app composition may own all lifetime groups

Do not encode aspirational rules that the current architecture cannot consistently obey.

## Recommended execution order

1. Add characterization tests.
2. Extract `PendingBridgeEdit`.
3. Extract `PendingElementEdit`.
4. Extract `WorkspaceInteractionRuntime`.
5. Extract grid and material-projection editing.
6. Narrow panel dependencies.
7. Group `EditorServices` lifetime domains, cook session included.
8. Update `editor/ARCHITECTURE.md` and layering checks to match the landed structure. That document has not been touched since before the cook, baked-lighting, and world-graph work landed, so this step also corrects its subsystem table and its one-line description of what `EditorServices` owns.
