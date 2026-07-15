# Kyusu Editor Decomposition Plan

Status: proposed execution plan

This plan is intentionally editor-only. Runtime and ECS stabilization live on the separate `agent/runtime-world-stabilization` branch.

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

## Phase 0: Characterization

Before moving state, add focused tests for:

- pending bridge begin, regenerate, commit, and cancel
- pending inset and bevel begin, parameter update, commit, and cancel
- cancellation when the edited entity or document disappears
- interaction rebuild after document changes
- grid origin, face alignment, rotation, and reset
- material projection copy, paste, and apply
- repeat-last-action behavior

Prefer behavior tests around the extracted mechanism. Avoid broad UI snapshots.

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

`EditorWorkspace` supplies the active document/session inputs and requests rebuilds. It should not individually own every node in this graph.

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

### `MaterialProjectionEditing`

Own:

- active material state
- UV projection clipboard
- apply material to selected faces
- copy projection
- paste projection

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

`EditorServices` remains the sole top-level owner and explicit bring-up/teardown sequence. Do not add `IProjectSession`, `IEditorRuntime`, factories, or a service locator.

## Dependency enforcement

Extend the existing editor layering checks only when an extracted boundary can be stated mechanically. Useful rules include:

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
7. Group `EditorServices` lifetime domains.
8. Update `editor/ARCHITECTURE.md` and layering checks to match the landed structure.
