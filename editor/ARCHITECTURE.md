# Sencha Editor Family: Architecture Map

A current-state map of how the editor tooling is laid out, so a change can start
from "where does this live" instead of a file hunt. For the original design
intent see `docs/SenchaEditor.md` (the pre-implementation spec; it predates the
code and has drifted, so where they differ the code is the source of truth).

## Big picture

The editor tooling is a family of applications over one shared shell library:

| Tree | Target | What it is |
| --- | --- | --- |
| `editor/common/` | `editor_common` (static lib) | The shared editor shell: ImGui UI feature + theme/skin, generic input, commands/selection/tools/interaction abstractions, offscreen viewport targets, and the project layer (descriptor, argv resolution, content mounting, process spawning). |
| `editor/kyusu/` | `kyusu` | The level editor ("Kyusu - Level Editor"). Everything below is about its internals. |
| `editor/chakin/` | `chakin` | The material editor ("Chakin - Material Editor"): browse/edit/save `.smat` with a live MeshForwardPass preview. |
| `editor/kettle/` | `kettle` | The project launcher ("Kettle - Project Launcher"): recent projects, create project, project settings, launches the editors. |

Product names (Kyusu, Chakin, Kettle) exist only on executables and window
titles; internal types stay mechanically named.

Every application is a `Game` running inside the runtime `Engine`. It does not
embed or wrap the engine; it shares the engine's window, Vulkan context,
console, and logging, and extends the engine by adding render features and
frame systems. The engine never depends on editor code (one-way dependency).

Each application follows the same two-file entry pattern:

- A `Game` lifecycle adapter (`app/EditorApp`, `MaterialEditorApp`,
  `LauncherApp`). Glue: each hook (`OnStart`, `OnRegisterSystems`,
  `OnPlatformEvent`, `OnShutdown`) forwards to the services object.
- A composition root (`app/EditorServices`, `MaterialEditorServices`,
  `LauncherServices`) that owns every subsystem and wires them. Kyusu's
  constructor is the bring-up sequence, split into named phases:
  `BuildDocument` -> `BuildPlayLoop` -> `BuildFileActions` -> `BuildInput` ->
  `BuildViewportRendering` -> `BuildUi`. Member order is teardown order; the
  destructor reproduces the load-bearing sequence explicitly.

## Projects

A project (`.senchaproj`, `ProjectDescriptor` in `common/src/project/`) names
the game module and the content roots. Editors resolve it via `--project
<path>` argv first, then the `SENCHA_PROJECT` env var (`ProjectArgs`), and
mount every content root (authored scan + `.cooked` overlay + on-demand texture
cook + asset id map) through `ProjectContentMount`, the same resolution the
runtime uses. Kettle spawns editors with `--project` via `ProcessLaunch`; the
same helper drives PIE's out-of-process player.

Materials (and assets generally) resolve against the project's content roots,
never against the open level file's location. Kyusu watches `.smat`/`.png`
sources per content root (`AssetSourceWatcher` + `AssetHotReloader`, polled
from the frame hook) and hot-swaps resident assets in place, so a save from
Chakin or a text editor shows up live.

## Include convention

Includes are src-root-relative (`ui/EditorUiFeature.h`, `brush/BrushMesh.h`).
Each application has its own `src/` and `editor_common`'s `src/` on the include
path; application src dirs are never on `editor_common`'s path, so the shell
cannot include an application header (enforced at compile time and by
`scripts/check_editor_layering.sh`).

## Layers (Kyusu)

Read the level editor bottom to top. Each layer depends only on layers below it.

1. Engine (external): window, Vulkan, console, logging, ECS, assets.
2. Core abstractions: `common/commands/`, `common/selection/`, `common/tools/`,
   `common/interaction/`, `kyusu/brush/`. Self-contained, no editor-domain
   dependencies. `brush/` is the half-edge geometry kernel and a pure leaf
   (engine-only).
3. Authoring subsystems: `input/`, `editmodes/`, `meshedit/`, `viewport/`,
   `render/`, and the `document/` domain. Each owns one slice of authoring.
4. Workspace aggregator: `workspace/`. `EditorWorkspace` is the per-document
   session: it composes the document plus every layer-3 subsystem into the shared
   state panels and tools read. It is the editor's central hub by design, so it
   has the widest fan-out; that breadth lives here, not scattered.
5. App composition: `app/`. `EditorServices` owns the workspace, input, UI, and
   play loop, and wires them into the engine.

## Subsystem map

Shared shell (`editor/common/src/`):

| Directory | Owns | Extension seam |
| --- | --- | --- |
| `commands/` | Generic undo/redo infrastructure (`CommandStack`, `CompositeCommand`). | `ICommand` |
| `selection/` | Multi-element selection model (`SelectionService`, `SelectionContext`, `SelectableRef`). | -- |
| `tools/` | Tool framework (`ToolRegistry`, `ToolContext`). | `ITool` |
| `interaction/` | Drag-interaction host (`InteractionHost`). | `IInteraction` |
| `input/` | Generic input primitives (`InputRouter` handler chain + pointer capture, `ShortcutRegistry`, `KeymapFile`, `UiInputGuard`). | router handlers |
| `ui/` | ImGui shell (`EditorUiFeature`: context, docking, menu, per-app ini), theme/skin (`EditorUiStyle`, `EditorThemeFile`, `EditorThemeStartup`, `ThemePreferences`), console panel, `ScopedPanel`, `SchemaWidgets`. | `IEditorPanel` |
| `render/` | Offscreen per-view targets (`ViewportTargetCache`). | -- |
| `viewport/` | `ViewportId`. | -- |
| `project/` | Project descriptor + resolution + mounting + spawning (`Project`, `ProjectArgs`, `ProjectContentMount`, `ProcessLaunch`, `MaterialLibrary`). | -- |

Level editor (`editor/kyusu/src/`):

| Directory | Owns | Extension seam |
| --- | --- | --- |
| `app/` | Entry point + composition root (`EditorApp`, `EditorServices`, `EditorFrameHook`, source hot-reload wiring). | -- |
| `workspace/` | The per-document authoring hub (`EditorWorkspace`, `BrushManipulationSink`). | -- |
| `brush/` | Half-edge brush geometry kernel: mesh, ops, tessellation, validation. Pure leaf (engine-only), consumed by `document`, `meshedit`, `render`, `ui`, `editmodes`, interactions, and the test suite. | -- |
| `input/` | Viewport-coupled input (`ViewportNavigation`, `ViewportToolDispatcher`, `SdlEventTranslation`). | -- |
| `editmodes/` | Transform gizmos and manipulator sessions (`TranslateManipulator`, `BoundsManipulator`, `EditSessionHost`). | manipulators |
| `meshedit/` | Polygon mesh-editing verbs (`MeshEditService`). | `IMeshEditTarget` |
| `viewport/` | Viewport layout, camera, picking (`ViewportLayout`, `EditorCamera`, `EditorViewportCameraSystem`, `Picking`). | -- |
| `render/` | Viewport render features and pipelines (`EditorRenderFeature`, grid/gizmo/selection/solid passes, the 14 embedded shaders). | `IRenderFeature` (engine) |
| `ui/` | The level editor's panels + chrome (viewport, inspector, hierarchy, mesh edit, material, toolbar, status bar, tool sidebar). | `IEditorPanel` |
| `document/` | Scene/document domain (see below). | -- |
| `project/` | Play-In-Editor (`PieDriver`, `PieSession`). | -- |

Material editor (`editor/chakin/src/`, flat): `MaterialEditorApp` +
`MaterialEditorServices`, `MaterialEditSession` (open/edit/save/duplicate,
headless-tested), `EditMaterialCommand`, `PreviewPrimitives` (procedural
sphere/cube/plane), `MaterialPreviewRenderFeature` (MeshForwardPass into a
ViewportTargetCache slot), and the browser/inspector/preview panels. Live
preview swaps the working description into the resident material via
`MaterialAssetLoader::CommitReload`.

Launcher (`editor/kettle/src/`, flat): `LauncherApp` + `LauncherServices`,
`ProjectCatalog` (recent projects JSON in the user config dir,
headless-tested), `ProjectBrowserPanel` (recent list, create form, settings
editor).

### Inside `document/` (the document domain)

- Document/scene model: `EditorDocument`, `EditorScene`, `DocumentSerialization`,
  `EntitySnapshot`.
- Cook: `DocumentCook` (`CookDocument` turns an `EditorDocument` into a runtime
  level file), `BrushCookInput`, `BrushBake`.
- File actions: `DocumentFileActions` (dialogs, content-root material rescan,
  unresolved-ref logging), `AssetFieldIo`.
- `document/commands/`: entity-edit commands (`ICommand` implementations).
- `document/tools/`: built-in tools (`SelectTool`, `BrushTool`, `EdgeCutTool`,
  `FaceCarveTool`).
- `document/interactions/`: tool-driven drag interactions (`IInteraction`).

The authoring session that ties these to tools, mesh edit, viewport, and render
lives one layer up, in `workspace/EditorWorkspace`, not here.

Naming note: the editor edits a `Document`; cooking it produces a runtime *level*
artifact, so the on-disk format keeps that vocabulary (`CookDocument` writes
`levels/<name>.level.json`). The "level" vocabulary is intentional only at that
format boundary; the editor's own types use document/scene vocabulary.

## Dependency rules

- Editor depends on engine, never the reverse.
- `editor_common` never includes an application-only subsystem; applications
  link `editor_common`, never each other.
- Each application's `app/` (or services) layer sits on top; it composes
  everything and is depended on by nothing.
- Core abstractions (`commands/`, `selection/`, `tools/`, `interaction/`,
  `brush/`) have no editor-domain dependencies; domain code depends on them, not
  the reverse.
- Cross-subsystem composition belongs in `workspace/` (the aggregator), so the
  layer-3 subsystems stay independent of each other.
- Domain commands live next to their domain (`document/commands/`), not in the
  generic `commands/` directory.

Enforced by `scripts/check_editor_layering.sh` (a ctest) plus the include-path
firewall above.

## Where do I add ...

- A panel: implement `IEditorPanel` (kyusu panels in `kyusu/src/ui/`), register
  it in the owning services' `BuildUi`.
- A tool: implement `ITool` (built-ins live in `kyusu/src/document/tools/`),
  register it with the `ToolRegistry` populated in `workspace/EditorWorkspace`.
- An undo-able edit: implement `ICommand` next to its domain, run it through the
  `CommandStack`.
- A keyboard shortcut: `EditorServices::BuildInput` (Kyusu); Chakin handles its
  few chords directly in `HandlePlatformEvent`.
- A viewport visual: a render feature/pass in `kyusu/src/render/`, added in
  `EditorServices::BuildViewportRendering`.
- A tunable: a cvar registered where it is read (see `editor.cull_backfaces` in
  `BuildViewportRendering`).
- A new editor application: a new `editor/<name>/` subdirectory linking
  `editor_common`, following the app-adapter + services pattern; add it in
  `editor/CMakeLists.txt`.
