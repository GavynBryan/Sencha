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
| `editor/level_editor/` | `level_authoring` + `level_editor` (static libs) | The level editor. Split in two: the authoring library (document, brush kernel, mesh edit, workspace, edit modes, viewport math, cook) is GUI- and Vulkan-free and is what the headless test targets link; the `level_editor` library is the shell over it (composition root, panels, render passes, SDL and window plumbing). Everything below is about its internals. |
| `editor/kyusu/` | `kyusu` | The Kyusu executable: the entry point and `Game` adapter that hosts the level editor. |
| `editor/material_editor/` | `material_editor` (static lib) + `shudei` | The material editor ("Shudei - Material Editor"): browse/edit/save `.smat` with a live MeshForwardPass preview. |
| `editor/project_browser/` | `project_browser` (static lib) | The Project workspace: recent projects, create project, project settings. Opening a project starts Kyusu again on it and ends the choosing process. Kyusu opens on it when started without a project. |
| `editor/ui_preview/` | `ui_preview_authoring` + `ui_preview` (static libs) + `shoji` | The authored-UI previewer ("Shoji - UI Previewer"): renders an `.rml` document through the engine's own UI pass into a panel at a chosen resolution and display scale, re-cooks and rebuilds it on save, and inspects elements, the preview model, raised actions and the layer's diagnostics. Same split as Kyusu: the authoring library (`DocumentLibrary`, `UiPreviewModel` and its `.preview.json` sidecar, `UiPreviewSession`, `BindingMisses`) is GUI-free; the executable is the shell. Built to fold into Kyusu: every panel takes a `UiPreviewSession&`, and consolidation is constructing one in Kyusu's composition root and adding these panels under a workspace tab. |

Product names (Kyusu, Shudei) exist only on executables and
window titles; internal types stay mechanically named.

Every application is a `Game` running inside the runtime `Engine`. It does not
embed or wrap the engine; it shares the engine's window, Vulkan context,
console, and logging, and extends the engine by adding render features and
frame systems. The engine never depends on editor code (one-way dependency).

## Kyusu and its workspaces

Kyusu (`editor/kyusu/`, `KyusuApp`) is the one `Game`. It owns the
`ProjectSession`, the window's `EditorUiFeature` and a `WorkspaceHost`
(`common/src/workspaces/`) built from the kinds table in
`kyusu/src/app/WorkspaceKinds.cpp`. A workspace (`IWorkspace`) is built when
first opened and destroyed when closed; the window draws the active one's
`WorkspaceView` (its panels, chrome, overlays, file actions and tab-strip
controls) under the `WorkspaceBar` tab strip and nothing of the others.
Opening, closing and switching are requests applied at the frame boundary by
the one `WorkspaceTickSystem`, since a workspace adds and removes render
features as it comes and goes; each open workspace is ticked there too.

Documents share one journal, the session's `DocumentSourceSet`: every
workspace's documents are sources in it (the level's is `LevelDocumentSource`,
one document for the whole world since a command may span zones). Undo retakes
the newest step wherever it is and brings the owning workspace forward first;
a workspace's own staged edit, such as the level's live preview, is undone by
itself before that. A workspace with changed documents asks Save, Discard or
Keep editing before it closes, the exit prompt does the same for all of them,
and a source destroyed with changes is a debug assertion: whoever drops them
does so deliberately. Cook and Play save every document first and stop on a
conflict or a failure. Saving a document whose file changed on disk since it
was read is a conflict the author settles, never an overwrite.

Each panel is given a window identity `<kind>.<settings id>` and its
remembered visibility is filed under `<kind>/<settings id>`, so two
workspaces' panels never share a window or a setting. The host adds a console
to every workspace it adopts.

The level workspace (`level_editor/src/app/LevelWorkspace`) is the level
editor's composition root. Its constructor is the bring-up sequence, split
into named phases: `BuildDocument` -> `BuildPlayLoop` -> `BuildFileActions` ->
`BuildInput` -> `BuildViewportRendering` -> `BuildUi`. Member order is teardown
order; the destructor reproduces the load-bearing sequence explicitly.

The other editors still run as their own executables, each with a `Game`
adapter (`MaterialEditorApp`, `DataEditorApp`, ...) forwarding to a services
object that owns and wires its subsystems.

## Projects

A project (`.senchaproj`, `ProjectDescriptor` in `common/src/project/`) names
the game module and the content roots. Editors resolve it via `--project
<path>` argv first, then the `SENCHA_PROJECT` env var (`ProjectArgs`), and
mount every content root (authored scan + `.cooked` overlay + on-demand texture
cook + asset id map) through `ProjectContentMount`, the same resolution the
runtime uses. The Project workspace starts Kyusu on a chosen project with
`--project` via `ProcessLaunch`; the same helper drives PIE's out-of-process
player.

Materials (and assets generally) resolve against the project's content roots,
never against the open level file's location. Kyusu mounts the project into
the engine's asset stack once (`ProjectSession`) and watches its authored
sources there, hot-swapping resident assets in place, so a save from any tool
shows up live. The assembly -- watcher, reloader, importer set, throttled poll
-- is the engine's `SourceReloadRoots` (`assets/hotreload/`), which
`RuntimeContent` owns and polls; an editor adds roots to
`RuntimeContent::SourceReload()`.

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
   `common/interaction/`, `level_editor/brush/`. Self-contained, no editor-domain
   dependencies. `brush/` is the half-edge geometry kernel and a pure leaf
   (engine-only).
3. Authoring subsystems: `input/`, `editmodes/`, `meshedit/`, `viewport/`,
   `render/`, and the `document/` domain. Each owns one slice of authoring.
4. Workspace aggregator: `workspace/`. `EditorWorkspace` composes the document
   plus every layer-3 subsystem into the shared state panels and tools read, and
   owns the mechanisms beside it: `WorkspaceInteractionRuntime` (the editing
   stack), `PendingBridgeEdit` and `PendingElementEdit` (staged previews),
   `SelectionActions` (verbs over the selection as a whole), `GridEditing`. It
   is the editor's central hub by design, so it has the widest fan-out; that
   breadth lives here, not scattered.
5. App composition: `app/`. `LevelWorkspace` owns the authoring hub, input,
   panels and play loop, and wires them into the engine.

## Subsystem map

Shared shell (`editor/common/src/`):

| Directory | Owns | Extension seam |
| --- | --- | --- |
| `commands/` | Generic undo/redo infrastructure (`CommandStack`, `CompositeCommand`). | `ICommand` |
| `selection/` | Multi-element selection model (`SelectionService`, `SelectionContext`, `SelectableRef`). | -- |
| `tools/` | Tool framework (`ToolRegistry`, `ToolContext`). | `ITool` |
| `interaction/` | Drag-interaction host (`InteractionHost`). | `IInteraction` |
| `input/` | Generic input primitives (`InputRouter` handler chain + pointer capture, `ShortcutRegistry`, `KeymapFile`, `UiInputGuard`). | router handlers |
| `ui/` | ImGui shell (`EditorUiFeature`: context, docking, menu, chassis, per-app ini), theme (`EditorUiStyle`: palette, metrics, decor, scale, text roles; `EditorThemeFile`, `EditorThemeStartup`, `ThemePreferences`), console panel, `ScopedPanel` (the one hook a panel's chrome comes through), `SchemaWidgets`. | `IEditorPanel` |
| `ui/chrome/` | The workstation chrome, one mechanism per file: geometry, painters, panel frames (`PanelStyle`), chassis, headers, bars and modules (the bar chassis of rims, recessed channel, end caps and lane; themed channel surfaces; readout cells, dividers, module bays), controls (buttons, combo housing), tiles, selection scope and marks, ornaments, icons (baked from `editor/icons/*.svg`), decor. Panels include only the panel-facing headers (rule D in `check_editor_layering.sh`). | edit an SVG in `editor/icons/` |
| `ui/ThemeTextureCache` | The raster art a theme owns, keyed by path and source stamp, with its own GPU lifetime. Deliberately not the font atlas: a theme switch costs one upload, not a font rebuild. | add a texture path to a theme's `surfaces` |
| `icons/` | `IconId`, the leaf enum a tool or control names an icon by. | -- |
| `render/` | ImGui presentation of offscreen targets (`ImGuiTargetPresenter`). | -- |
| `viewport/` | `ViewportId`. | -- |
| `project/` | Project descriptor + resolution + mounting + spawning (`Project`, `ProjectArgs`, `ProjectContentMount`, `ProcessLaunch`, `MaterialLibrary`). | -- |

Level editor (`editor/level_editor/src/`):

| Directory | Owns | Extension seam |
| --- | --- | --- |
| `app/` | The composition root (`LevelWorkspace`) and `EditorCookRuntime` (the cook session, the player it feeds, and the serials that hand one to the other). | -- |
| `workspace/` | The per-document authoring hub (`EditorWorkspace`, `BrushManipulationSink`) plus the mechanisms it composes: `WorkspaceInteractionRuntime`, `PendingBridgeEdit`, `PendingElementEdit`, `SelectionActions`, `GridEditing`, `EscapePolicy`. | -- |
| `brush/` | Half-edge brush geometry kernel: mesh, ops, tessellation, validation. Pure leaf (engine-only), consumed by `document`, `meshedit`, `render`, `ui`, `editmodes`, interactions, and the test suite. | -- |
| `input/` | Viewport-coupled input (`ViewportNavigation`, `ViewportToolDispatcher`, `SdlEventTranslation`). | -- |
| `editmodes/` | Transform gizmos and manipulator sessions (`TranslateManipulator`, `BoundsManipulator`, `EditSessionHost`). | manipulators |
| `meshedit/` | Polygon mesh-editing verbs (`MeshEditService`). | `IMeshEditTarget` |
| `viewport/` | Viewport layout, camera, picking (`ViewportLayout`, `EditorCamera`, `EditorViewportCameraSystem`, `Picking`). | -- |
| `render/` | Viewport render features and pipelines (`EditorRenderFeature`, grid/gizmo/selection/solid passes, the 14 embedded shaders). | `IRenderFeature` (engine) |
| `ui/` | The level editor's panels + chrome (viewport, inspector, hierarchy, mesh edit, material, tool palette, toolbar, status bar). | `IEditorPanel` |
| `document/` | Scene/document domain (see below). | -- |
| `project/` | Play-In-Editor (`PieDriver`, `PieSession`). | -- |

Material editor (`editor/material_editor/src/`, flat): `MaterialEditorApp` +
`MaterialEditorServices`, `MaterialEditSession` (open/edit/save/duplicate,
headless-tested), `EditMaterialCommand`, `PreviewPrimitives` (procedural
sphere/cube/plane), `MaterialPreviewRenderFeature` (MeshForwardPass into its own
offscreen target), and the browser/inspector/preview panels. Live
preview swaps the working description into the resident material via
`MaterialAssetLoader::CommitReload`.

Project workspace (`editor/project_browser/src/`, flat): `ProjectWorkspace`,
`ProjectCatalog` (recent projects JSON in the user config dir,
headless-tested), `ProjectBrowserPanel` (recent list, create form, settings
editor), `ProjectRelaunch` (the command that opens a project in a new process).

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
- Workspace trees (level, material, data, animation, UI preview, project
  browser) never include one another's headers; only `editor/kyusu` composes
  them.
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
firewall above. One deliberate exception: a panel may include the narrow
workspace mechanism it drives (`PendingBridgeEdit`, `PendingElementEdit`,
`SelectionActions`) rather than receive a bag of callbacks assembled for it.
`EditorWorkspace` itself stays off-limits to `ui/`.

## Where do I add ...

- A panel: implement `IEditorPanel` (level editor panels in `level_editor/src/ui/`), register
  it in the owning services' `BuildUi`. It declares a stable settings id and
  whether its shown/hidden state is remembered across launches
  (`GetPersistence`); the shell keeps that in the ImGui layout file.
- A tool: implement `ITool` (built-ins live in `level_editor/src/document/tools/`) and
  register it in `WorkspaceInteractionRuntime::Rebuild`. That is the whole cost:
  a tool declares its own properties UI (`DrawProperties`), toolbar chrome
  (`DrawToolbarControls`), activation key (`GetShortcut`), and how a save should
  resolve anything it has staged (`CommitPending`), so the panel, the toolbar,
  the tool palette, the tool wheel, the status bar, and the keymap all pick it
  up without an edit. The toolbar is not a bar of its own: the perspective
  viewport's header is the toolbar row it reserves in place of a title
  (`ViewportPanel::SetHeaderRows`), with the gizmo strip centred on the
  window's midline; the cook/play loop (`CookPlayControls`) sits at the right of the `WorkspaceBar` tab strip under the caption, the
  plate that will carry workspace tabs.
  Settings only that tool acts on are members on the tool; genuinely shared
  authoring state (the grid, the active material) goes through `ToolContext`.
  A tool with sub-modes exposes them as variants (`GetVariants`,
  `GetActiveVariant`, `SelectVariant`): a label and an icon each, addressed by
  index, resolved by the tool against whatever owns the mode (the select
  tool's is `MeshEditService`'s element kind, the brush tool's its own
  primitive). The tool wheel shows them as a fan outside its rim while the tool is
  hot and the properties row draws them; neither learns the type behind them.
- An undo-able edit: implement `ICommand` next to its domain, run it through the
  `CommandStack`.
- A keyboard shortcut: the binding table in `LevelWorkspace::BuildInput` (the level editor);
  Shudei handles its few chords directly in `HandlePlatformEvent`. Tool
  activation rows are generated from the registry instead, under `tool.<id>`.
  Any action, listed or generated, is rebindable from `keybinds.json`. A held
  key is owned by a `RadialMenuSession` rather than the shortcut registry,
  which fires on presses; it resolves its override from the same file. There is
  one radial-menu mechanism (`RadialMenuMath`, `RadialMenuSession`, the chrome's
  `DrawRadialMenu`) over `IRadialMenuModel`; the tools are one model
  (`tool.wheel`, `ToolRegistryMenuModel`), the gizmo modes another
  (`gizmo.wheel`, `TransformModeMenuModel` over the `TransformModeItems`
  table the toolbar strip reads too). An open wheel is modal, which is what
  keeps the other closed.
- A viewport visual: a render feature/pass in `level_editor/src/render/`, added in
  `LevelWorkspace::BuildViewportRendering`.
- A tunable: a cvar registered where it is read (see `editor.cull_backfaces` in
  `BuildViewportRendering`).
- A new editor application: a new `editor/<name>/` subdirectory linking
  `editor_common`, following the app-adapter + services pattern; add it in
  `editor/CMakeLists.txt`.
