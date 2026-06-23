# Sencha Editor — Architecture Map

A current-state map of how the editor is laid out, so a change can start from
"where does this live" instead of a file hunt. For the original design intent see
`docs/SenchaEditor.md` (the pre-implementation spec; it predates the code and has
drifted, e.g. its `document/` is today's `level/`).

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

## Subsystem map

| Directory | Owns | Extension seam |
| --- | --- | --- |
| `app/` | Entry point + composition root (`EditorApp`, `EditorServices`, `EditorFrameHook`). | -- |
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
| `level/` | Scene authoring domain (see below). | -- |
| `project/` | Project descriptor + Play-In-Editor (`PieDriver`, `PieSession`). | -- |

### Inside `level/` (the largest subsystem)

- Document/scene model: `LevelDocument`, `LevelScene`, `LevelSerialization`,
  `EntitySnapshot`.
- Aggregator: `LevelWorkspace` bundles the per-document authoring state (layout,
  selection, picking, mesh edit, tools, interactions, grid) that panels and tools
  share.
- Cook: `LevelCook`, `BrushCookInput`.
- File + materials: `DocumentFileActions`, `MaterialLibrary`, `AssetFieldIo`.
- `level/brush/`: the half-edge brush geometry kernel (mesh, ops, tessellation,
  validation). It is a pure leaf (depends only on the engine) and is consumed by
  `level`, `meshedit`, `render`, `ui`, `editmodes`, and interactions. It reads as
  level-specific but is actually a shared kernel.
- `level/commands/`: entity-edit commands (`ICommand` implementations).
- `level/tools/`: built-in tools (`SelectTool`, `BrushTool`, `CameraTool`).
- `level/interactions/`: tool-driven drag interactions (`IInteraction`).

## Dependency rules

- Editor depends on engine, never the reverse.
- `app/` sits on top; it composes everything and is depended on by nothing.
- Generic infrastructure (`commands/`, `selection/`, `tools/`, `interaction/`)
  has no editor-domain dependencies; domain code depends on it, not the reverse.
- Domain commands live next to their domain (`level/commands/`), not in the
  generic `commands/` directory.

## Where do I add ...

- A panel: implement `IEditorPanel` in `ui/`, register it in
  `EditorServices::BuildUi`.
- A tool: implement `ITool` (built-ins live in `level/tools/`), register it with
  the `ToolRegistry` populated in `LevelWorkspace`.
- An undo-able edit: implement `ICommand` next to its domain, run it through the
  `CommandStack`.
- A keyboard shortcut: `EditorServices::BuildInput`.
- A viewport visual: a render feature/pass in `render/`, added in
  `EditorServices::BuildViewportRendering`.
- A tunable: a cvar registered where it is read (see `editor.cull_backfaces` in
  `BuildViewportRendering`).
