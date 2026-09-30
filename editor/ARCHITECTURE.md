# Sencha Editor Family: Architecture Map

A current-state map of how the editor tooling is laid out, so a change can start
from "where does this live" instead of a file hunt. For the original design
intent see `docs/SenchaEditor.md` (the pre-implementation spec; it predates the
code and has drifted, so where they differ the code is the source of truth).

## Big picture

The editor is one application, Kyusu, over one shared shell library; each kind
of editing it offers is a workspace library:

| Tree | Target | What it is |
| --- | --- | --- |
| `editor/common/` | `editor_common` (static lib) | The shared editor shell: ImGui UI feature + theme/skin, generic input, commands/selection/tools/interaction abstractions, offscreen viewport targets, and the project layer (descriptor, argv resolution, content mounting, process spawning). |
| `editor/level_editor/` | `level_authoring` + `level_editor` (static libs) | The level editor. Split in two: the authoring library (document, brush kernel, mesh edit, workspace, edit modes, viewport math, cook) is GUI- and Vulkan-free and is what the headless test targets link; the `level_editor` library is the shell over it (composition root, panels, render passes, SDL and window plumbing). Everything below is about its internals. |
| `editor/kyusu/` | `kyusu` | The Kyusu executable and composition root: `KyusuApp`, the `ProjectSession`, and the table of workspace kinds. The only tree that sees every workspace. |
| `editor/data_editor/` | `data_editor` (static lib) | The Data workspace: schema-driven forms over typed `.sdata` documents. |
| `editor/animation_editor/` | `animation_authoring` + `animation_editor` (static libs) | The Animation workspace: rig, selector and clip-event authoring over a live skinned preview and a scenario lab. |
| `editor/material_editor/` | `material_editor` (static lib) | The Materials workspace: browse, edit and save `.smat` with a live MeshForwardPass preview. |
| `editor/project_browser/` | `project_browser` (static lib) | The Project workspace: recent projects, create project, project settings. Opening a project starts Kyusu again on it and ends the choosing process. Kyusu opens on it when started without a project. |
| `editor/ui_preview/` | `ui_preview_authoring` + `ui_preview` (static libs) | The UI Preview workspace: renders an `.rml` document through the engine's own UI pass into a panel at a chosen resolution and display scale, re-cooks and rebuilds it on save, and inspects elements, the preview model, raised actions and the layer's diagnostics. The authoring library (`DocumentLibrary`, `UiPreviewModel` and its `.preview.json` sidecar, `UiPreviewSession`, `BindingMisses`) is GUI-free; the shell library holds `UiPreviewWorkspace` and its panels. |

Product names (Kyusu) exist only on executables and
window titles; internal types stay mechanically named.

Kyusu is a `Game` running inside the runtime `Engine`. It does not embed or
wrap the engine; it shares the engine's windows, Vulkan context, console, and
logging, and extends the engine by adding render features and frame systems.
The engine never depends on editor code (one-way dependency).

## Kyusu and its workspaces

Kyusu (`editor/kyusu/`, `KyusuApp`) is the one `Game`. It owns the
`ProjectSession`, each window's `ShellWindow` and `EditorUiFeature`, and a
`WorkspaceHost` (`common/src/workspaces/`) built from the kinds table in
`kyusu/src/app/WorkspaceKinds.cpp`. A workspace (`IWorkspace`) is built when
first opened and destroyed when closed. Each window's `ShellWindow`
(`common/src/shell/`, authored UI, `editor/ui/shell.rml`) is its rail: the
hatch listing every action in play, the workspace and document dials, the
problems lamp, the transport and the window keys, all fed by the active
workspace's facets (`WorkspaceFacets.h`) and actions. It also presents the
command palette (Ctrl+K) and the prompt queue's front question; every window
shows the same question, so it is answered wherever the person is. Under the rail the
window's `EditorUiFeature` docks the active workspace's ImGui `WorkspaceView`
panels until each workspace presents through the shell. Under the panels is
the window's content surface (`WorkspaceWindow::Content`, drawn in
`kContentUiLayer` by a `UiRenderFeature` ordered before the ImGui feature):
the Level draws its views there as host images (`LevelViewSurface`), laid out
by `ViewportArrangement` over the views panel's rect, and the panel stays
transparent where they show
(`docs/editor/interaction-grammar.md`). The views meet at a divider the
person drags (`ViewDividerDrag`, kept in the `editor.view.divider_x/_y`
cvars); the shell's transitions follow `editor.ui.motion`.
Opening, closing and switching are requests applied at the frame boundary by
the one `WorkspaceTickSystem`, since a workspace adds and removes render
features as it comes and goes; each open workspace is ticked there too.

Data documents live once per process in the session's `DataDocumentStore`;
each workspace shows them through a `DataDocumentSet` view (its tabs, active
document and selected field), so the Data and Animation workspaces showing one
file edit one document, and the store closes it when its last view lets go.

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

A workspace is placed in a window. The main window is the engine's primary
presentation; `workspace.detach` (or a tab dragged off the strip, or its "Move
to New Window") opens another through `OpenPresentationWindow` and draws the
workspace there with a detached `EditorUiFeature`: its own ImGui context and
backends over the primary's font atlas, style and layout, recorded into that
presentation's swapchain scope. Each window shows the workspace most recently
activated in it; closing a window, or `workspace.attach`, brings its
workspaces back, and a window left empty closes. Every ImGui texture set is
added and freed through the primary's backend (`ui/ImGuiTextureOwner`), since
a preview shown in one window may be released from another. Platform events
reach the shell and workspace of the window they name.

Offscreen previews render only while a panel shows them: the UI marks what it
displayed in a `DisplayLedger`, and the next frame renders and keeps only that
(`render/DisplayedTargets`; the level viewports read the same ledger). A
hidden workspace, a closed panel and a minimized window cost no target and no
recording.

The level workspace (`level_editor/src/app/LevelWorkspace`) is the level
editor's composition root. Its constructor is the bring-up sequence, split
into named phases: `BuildDocument` -> `BuildPlayLoop` -> `BuildFileActions` ->
`BuildInput` -> `BuildViewportRendering` -> `BuildUi`. Member order is teardown
order; the destructor reproduces the load-bearing sequence explicitly.

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
| `input/` | Generic input primitives (`InputRouter` handler chain + pointer capture, `KeymapFile`, `UiInputGuard`). | router handlers |
| `properties/` | The toolkit-free property model: `PropertyNode` trees (key, descriptor, value as text, children; no rows), `IPropertySource` (begin/preview/commit/cancel mapped onto a document's own transaction) and `PropertyEditSession` (the one edit in flight, ended by any interruption). The level's source is `authoring/ComponentPropertySource`. | inspectors, cards, drawers |
| `observers/` | `WeakObserverList`: subscribers held for as long as they keep their token. | selection, resources, problems |
| `resources/` | `ActiveResources`: what the person is holding, per asset kind, with a stash; changes reach the tool in hand through `ToolRegistry`. `ResourceActions` offers the stash as actions. | tools, workspaces |
| `problems/`, `notices/` | `ProblemList` (every located problem, republished whole by each source) and `NoticeQueue` (brief word that something happened). | the shell |
| `prompts/` | `PromptQueue` (questions that hold an operation, answered one at a time; an `Asker` withdraws its questions when it goes) and the document questions over it: `AskAboutChanges`, `DocumentExitHold` (exit waits on changed documents) and `DocumentSaveFollowUp` (a failed save is a problem, a conflict is a question). | workspaces, Kyusu |
| `presenters/` | Neutral models as the rows a document shows: `PropertySheet` turns property nodes into rows and a row's edit back into its source. Used by the shell and by documents a workspace owns. | shell, workspaces |
| `shell/` | The replaceable presentation policy's home (`docs/editor/interaction-grammar.md`); today `ShellWindow`, `CommandPalette`, `PromptDialog` and `TransientSurfaces`, the generic open/dismiss/Escape mechanism its popovers, drawers and explorers are presets of. Workspaces never include it. | Kyusu |
| `actions/` | The one table of operations (`ActionCatalog`: id, label, category, chords, no callbacks) and who performs them (`ActionContext` handlers, resolved per window by `ActionContextStack`, closest context first). | Kyusu, workspaces |
| `ui/` | ImGui shell (`EditorUiFeature`: context, docking, menu, chassis, per-app ini), theme (`EditorUiStyle`: palette, metrics, decor, scale, text roles; `EditorThemeFile`, `EditorThemeStartup`, `ThemePreferences`), console panel, `ScopedPanel` (the one hook a panel's chrome comes through), `SchemaWidgets`. | `IEditorPanel` |
| `ui/chrome/` | The workstation chrome, one mechanism per file: geometry, painters, panel frames (`PanelStyle`), chassis, headers, bars and modules (the bar chassis of rims, recessed channel, end caps and lane; themed channel surfaces; readout cells, dividers, module bays), controls (buttons, combo housing), tiles, selection scope and marks, ornaments, icons (baked from `editor/ui/icons/*.svg`), decor. Panels include only the panel-facing headers (rule D in `check_editor_layering.sh`). | edit an SVG in `editor/ui/icons/` |
| `icons/` | `IconId`, the leaf enum a tool or control names an icon by. | -- |
| `workspaces/` | The workspace model: `IWorkspace`, the `WorkspaceKind` row, `WorkspaceHost` (open, close, activate, window placement, requests at the frame boundary; headless-tested). | a row in `WorkspaceKinds.cpp` |
| `render/` | ImGui presentation of offscreen targets (`ImGuiTargetPresenter`), display-driven target lifetime (`DisplayLedger`, `DisplayedTargets`), `DetachRenderFeature`. | -- |
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
| `ui/` | The level editor's remaining ImGui panels (views, hierarchy, material) and its authored surfaces (`LevelViewSurface`). | `IEditorPanel` |
| `document/` | Scene/document domain (see below). | -- |
| `project/` | Play-In-Editor (`PieDriver`, `PieSession`). | -- |

Materials workspace (`editor/material_editor/src/`, flat): `MaterialWorkspace`,
`MaterialDocumentSet` (the open materials as journal documents, one tab and
one undo history each, headless-tested), `MaterialEditSession`
(open/edit/save/duplicate, conflict detection by file baseline),
`EditMaterialCommand`, `PreviewPrimitives` (procedural sphere/cube/plane),
`MaterialPreviewRenderFeature` (MeshForwardPass into its own offscreen
target), and the files/inspector/preview/textures panels. Live preview pushes
the working description into the resident material via `AssetSystem::Reload`;
a watched `.smat` that changes on disk is taken by a clean tab and kept out of
the stack while a tab holds its own changes (`DocumentSource::FileChangedOnDisk`
behind `SourceReloadRoots::SetReloadFilter`).

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
- No workspace includes `common/src/shell/`: a workspace exposes what it has
  (actions, properties, resources) and the shell, a replaceable presentation
  policy, decides how it appears. A document a workspace owns outright (a
  dialog, a bench) presents properties through `common/src/presenters/`, which
  the shell uses too; restyling the grammar rewrites both.
- ImGui is on its way out of the editor (`docs/editor/interaction-grammar.md`).
  Geometry, input, tools and viewport math are written in engine vectors
  (`Vec2d`); only files listed in `scripts/editor_imgui_allowlist.txt` may name
  ImGui, and the list only shrinks. `ui/ImGuiVec.h` converts at the boundary.

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
  a tool declares its settings as properties (`Parameters`, `SetParameter`),
  whether it has drafted work (`Draft`) and what to do next (`Hint`), its
  activation key (`GetShortcut`), and how a save should resolve anything it has
  staged (`CommitPending`). It draws nothing: the Level's operation facet hands
  the shell those facts and a `ToolParameterSource`, and the shell's station and
  tool card present them through `PropertySheet`, so the station, the card,
  the tool wheel and the keymap all pick it up without an edit. How the hands
  act -- element mode, gizmo mode and axes, pivot, snapping, the grid -- and
  what the views show are actions with a checked state
  (`workspace/ManipulationActions`), named for the readout by the manipulation
  and view facets; the readout's segments and the view instrument open their
  categories' actions. What the hands hold is the resources facet: per role,
  the active resource and its stash, each named and pictured by a host image
  (`MaterialThumbnailCache::ImageName`); the stash is worked through the
  `resource.*` actions (`resources/ResourceActions`). What is selected is the
  selection facet: a title, the actions that apply now, and property sections
  -- the Level's `ElementVerbs` (every brush, face, edge and vertex verb, and
  the settings the parametric ones read) and `FaceUvSource`, composed by
  `SelectionSummary`, with one object's components as its details
  (`authoring/ComponentPropertySource`; a component adapter contributes its own
  properties and verbs through `InspectorNodes`/`SetInspectorValue`/
  `ActInspector`). The shell's selection card presents it, and turns into the
  drawer that shows every property. What a workspace lets a person browse is
  an explorer (`WorkspaceExplorerInfo`, paged through `Explore` and chosen
  from with `ChooseInExplorer`); the Level's materials are
  `workspace/MaterialExplorer`, which the shell shows as a tray and opens from
  its `explorer.materials` action or the cartridge; its prefabs are
  `workspace/PrefabExplorer` (Shift+P), whose choice takes up the Place tool.
  A tool declares what it consumes (`ITool::Resources`), and the cartridge
  follows the tool in hand's primary role. Cook, play and
  stop are the `transport.*` actions the rail's transport shows.
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
- An operation a person can invoke: define it in the `ActionCatalog` (its id,
  label, category and default chords -- data, no callback) and claim it in the
  `ActionContext` of whatever performs it. The level workspace's table is in
  `LevelWorkspace::BuildInput`; the operations every workspace shares (undo,
  redo, save, save all) are Kyusu's, in `KyusuApp::DefineApplicationActions`.
  Kyusu puts the active workspace's context in play in its window, so a chord,
  a menu, the console (every action is a console command of its id) and later
  the palette all reach one table, and a chord belongs to one action whoever
  performs it. Tool activation is generated from the registry, under
  `tool.<id>`. Any action is rebindable from `keybinds.json`. A held key is
  owned by a `RadialMenuSession` rather than an action handler, which fires on
  presses; its chord is still the catalog's (`tool.wheel`, `gizmo.wheel`). There is
  one radial-menu mechanism (`RadialMenuMath`, `RadialMenuSession`, `FaceOf`
  for what an open wheel shows, and `RadialMenuScreen`, which draws it on the
  window's overlay surface) over `IRadialMenuModel`; the tools are one model
  (`tool.wheel`, `ToolRegistryMenuModel`), the gizmo modes another
  (`gizmo.wheel`, `TransformModeMenuModel` over the `TransformModeItems`
  table the Transform actions read too). An open wheel is modal, which is what
  keeps the other closed.
- A viewport visual: a render feature/pass in `level_editor/src/render/`, added in
  `LevelWorkspace::BuildViewportRendering`. A tool's world strokes go through
  `EditorOverlayState` (segments, dials, the drag readout), drawn into each view
  by `AnnotationStrokeRenderer`; what sits at a point (labels, handles, buttons,
  a dial's knob) becomes a `ViewMark` the views document places.
- A scripted walkthrough: `input.pointer`, `input.key` and `input.button` push
  platform events at a named frame, so an unattended run can hold a wheel open
  or click a view before `render.screenshot` captures it.
- A tunable: a cvar registered where it is read (see `editor.cull_backfaces` in
  `BuildViewportRendering`).
- A workspace: a library under `editor/<name>/` linking `editor_common`, with
  an `IWorkspace` that builds its panels into a `WorkspaceView`, and one row in
  `editor/kyusu/src/app/WorkspaceKinds.cpp` that constructs it with exactly
  the services it needs. It never includes another workspace's headers.
