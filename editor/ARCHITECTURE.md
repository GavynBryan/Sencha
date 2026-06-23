# Sencha Editor — Architecture Map

A current-state map of how the editor is laid out, so a change can start from
"where does this live" instead of a file hunt. For the original design intent see
`docs/SenchaEditor.md` (the pre-implementation spec; it predates the code and has
drifted, so where they differ the code is the source of truth).

## Big picture

The editor is a `Game` running inside the runtime `Engine`. It does not embed or
wrap the engine; it shares the engine's window, Vulkan context, console, and
logging, and extends the engine by adding render features and frame systems. The
engine never depends on editor code (one-way dependency).

Two files own the entry point:

- `app/EditorApp` is the `Game` lifecycle adapter. It is glue: each hook
  (`OnStart`, `OnRegisterSystems`, `OnPlatformEvent`, `OnShutdown`) forwards to
  `EditorServices`.
- `app/EditorServices` is the composition root. It owns every editor subsystem
  and wires them. Its constructor is the bring-up sequence, split into named
  phases: `BuildDocument` -> `BuildPlayLoop` -> `BuildFileActions` ->
  `BuildInput` -> `BuildViewportRendering` -> `BuildUi`. Member order is teardown
  order; the destructor reproduces the load-bearing sequence explicitly.

## Layers

Read the editor bottom to top. Each layer depends only on layers below it.

1. Engine (external): window, Vulkan, console, logging, ECS, assets.
2. Core abstractions: `commands/`, `selection/`, `tools/`, `interaction/`,
   `brush/`. Self-contained, no editor-domain dependencies. `brush/` is the
   half-edge geometry kernel and a pure leaf (engine-only).
3. Authoring subsystems: `input/`, `editmodes/`, `meshedit/`, `viewport/`,
   `render/`, and the `document/` domain. Each owns one slice of authoring.
4. Workspace aggregator: `workspace/`. `EditorWorkspace` is the per-document
   session: it composes the document plus every layer-3 subsystem into the shared
   state panels and tools read. It is the editor's central hub by design, so it
   has the widest fan-out; that breadth lives here, not scattered.
5. App composition: `app/`. `EditorServices` owns the workspace, input, UI, and
   play loop, and wires them into the engine.

## Subsystem map

| Directory | Owns | Extension seam |
| --- | --- | --- |
| `app/` | Entry point + composition root (`EditorApp`, `EditorServices`, `EditorFrameHook`). | -- |
| `workspace/` | The per-document authoring hub (`EditorWorkspace`, `BrushManipulationSink`). | -- |
| `brush/` | Half-edge brush geometry kernel: mesh, ops, tessellation, validation. Pure leaf (engine-only), consumed by `document`, `meshedit`, `render`, `ui`, `editmodes`, interactions, and the test suite. | -- |
| `commands/` | Generic undo/redo infrastructure (`CommandStack`, `CompositeCommand`). | `ICommand` |
| `selection/` | Multi-element selection model (`SelectionService`, `SelectionContext`, `SelectableRef`). | -- |
| `tools/` | Tool framework (`ToolRegistry`, `ToolContext`). | `ITool` |
| `input/` | Platform input routing (`InputRouter` handler chain + pointer capture, `ViewportNavigation`, `ShortcutRegistry`, `ViewportToolDispatcher`, SDL translation). | router handlers |
| `interaction/` | Drag-interaction host (`InteractionHost`). | `IInteraction` |
| `editmodes/` | Transform gizmos and manipulator sessions (`TranslateManipulator`, `BoundsManipulator`, `EditSessionHost`). | manipulators |
| `meshedit/` | Polygon mesh-editing verbs (`MeshEditService`). | `IMeshEditTarget` |
| `viewport/` | Viewport layout, camera, picking (`ViewportLayout`, `EditorCamera`, `EditorViewportCameraSystem`, `Picking`). | -- |
| `render/` | Viewport render features and pipelines (`EditorRenderFeature`, grid/gizmo/selection/solid passes). | `IRenderFeature` (engine) |
| `ui/` | ImGui chrome + panels host (`EditorUiFeature`, toolbar, status bar, skin, the panels). | `IEditorPanel` |
| `document/` | Scene/document domain (see below). | -- |
| `project/` | Project descriptor + Play-In-Editor (`PieDriver`, `PieSession`). | -- |

### Inside `document/` (the document domain)

- Document/scene model: `EditorDocument`, `EditorScene`, `DocumentSerialization`,
  `EntitySnapshot`.
- Cook: `DocumentCook` (`CookDocument` turns an `EditorDocument` into a runtime
  level file), `BrushCookInput`.
- File + materials: `DocumentFileActions`, `MaterialLibrary`, `AssetFieldIo`.
- `document/commands/`: entity-edit commands (`ICommand` implementations).
- `document/tools/`: built-in tools (`SelectTool`, `BrushTool`, `CameraTool`).
- `document/interactions/`: tool-driven drag interactions (`IInteraction`).

The authoring session that ties these to tools, mesh edit, viewport, and render
lives one layer up, in `workspace/EditorWorkspace`, not here.

Naming note: the editor edits a `Document`; cooking it produces a runtime *level*
artifact, so the on-disk format keeps that vocabulary (`CookDocument` writes
`levels/<name>.level.json`). The "level" vocabulary is intentional only at that
format boundary; the editor's own types use document/scene vocabulary.

## Dependency rules

- Editor depends on engine, never the reverse.
- `app/` sits on top; it composes everything and is depended on by nothing.
- Core abstractions (`commands/`, `selection/`, `tools/`, `interaction/`,
  `brush/`) have no editor-domain dependencies; domain code depends on them, not
  the reverse.
- Cross-subsystem composition belongs in `workspace/` (the aggregator), so the
  layer-3 subsystems stay independent of each other.
- Domain commands live next to their domain (`document/commands/`), not in the
  generic `commands/` directory.

## Where do I add ...

- A panel: implement `IEditorPanel` in `ui/`, register it in
  `EditorServices::BuildUi`.
- A tool: implement `ITool` (built-ins live in `document/tools/`), register it
  with the `ToolRegistry` populated in `workspace/EditorWorkspace`.
- An undo-able edit: implement `ICommand` next to its domain, run it through the
  `CommandStack`.
- A keyboard shortcut: `EditorServices::BuildInput`.
- A viewport visual: a render feature/pass in `render/`, added in
  `EditorServices::BuildViewportRendering`.
- A tunable: a cvar registered where it is read (see `editor.cull_backfaces` in
  `BuildViewportRendering`).
