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
| `editor/common/` | `editor_common` (static lib) | What every workspace shares: the shell (rail, stations, cards, explorers, prompts, palette, presented through Shoji), the theme, actions, properties, generic input, commands/selection/tools/interaction abstractions, offscreen targets shown as host images, and the project layer (descriptor, argv resolution, content mounting, process spawning). |
| `editor/level_editor/` | `level_authoring` + `level_editor` (static libs) | The level editor. Split in two: the authoring library (document, brush kernel, mesh edit, workspace, edit modes, viewport math, cook) is GUI- and Vulkan-free and is what the headless test targets link; the `level_editor` library is what needs a GPU and a window over it (composition root, the views and dialog surfaces, render passes, SDL and window plumbing). Everything below is about its internals. |
| `editor/kyusu/` | `kyusu` | The Kyusu executable and composition root: `KyusuApp`, the `ProjectSession`, and the table of workspace kinds. The only tree that sees every workspace. |
| `editor/data_editor/` | `data_editor` (static lib) | The Data workspace: schema-laid sheets over typed `.sdata` documents. |
| `editor/animation_editor/` | `animation_authoring` + `animation_editor` (static libs) | The Animation workspace: rig, selector and clip-event authoring over a live skinned preview and a scenario lab. |
| `editor/material_editor/` | `material_editor` (static lib) | The Materials workspace: browse, edit and save `.smat` with a live MeshForwardPass preview. |
| `editor/project_browser/` | `project_browser` (static lib) | The Project workspace: recent projects, create project, project settings. Opening a project starts Kyusu again on it and ends the choosing process. Kyusu opens on it when started without a project. |
| `editor/ui_preview/` | `ui_preview_authoring` + `ui_preview` (static libs) | The UI Preview workspace: renders an `.rml` document through the engine's own UI pass into a host image that fills the content (`ContentPicture`), at a chosen resolution, display scale and zoom, and re-cooks and rebuilds it on save. Interact hands the document the pointer; Inspect picks elements, which the card describes. With nothing picked the card is the surface and the bindings the model misses, and the details are the preview model, saved as the sidecar, a document of the journal like any other. The library, the element tree and the vocabulary are explorers, the layer's diagnostics and unresolved bindings are problems, and raised actions are notices. The authoring library (`DocumentLibrary`, `UiPreviewModel` and its `.preview.json` sidecar, `UiPreviewSession`, `BindingMisses`, `PreviewFraming`, `PreviewReports`, `ElementOutline`, `LibraryOutline`, `VocabularyOutline`, `ElementProperties`, `SurfaceProperties`, `PreviewModelProperties`, `BindingMissProperties`) is GUI-free; the workspace library holds `UiPreviewWorkspace` alone. |

Product names (Kyusu) exist only on executables and
window titles; internal types stay mechanically named.

Kyusu is a `Game` running inside the runtime `Engine`. It does not embed or
wrap the engine; it shares the engine's windows, Vulkan context, console, and
logging, and extends the engine by adding render features and frame systems.
The engine never depends on editor code (one-way dependency).

## Kyusu and its workspaces

Kyusu (`editor/kyusu/`, `KyusuApp`) is the one `Game`. It owns the
`ProjectSession`, each window's `ShellWindow`, the `ThemeSheet`, and a
`WorkspaceHost` (`common/src/workspaces/`) built from the kinds table in
`kyusu/src/app/WorkspaceKinds.cpp`. A workspace (`IWorkspace`) is built when
first opened and destroyed when closed. Each window's `ShellWindow`
(`common/src/shell/`, authored UI, `editor/ui/shell.rml`) is its rail: the
hatch listing every action in play, a tab per asset open in the editors placed
in the window (`shell/AssetTabs`: choosing one shows its editor and the asset;
an editor with nothing open is a tab of its own; "+" opens another editor), the
problems lamp, the transport and the window keys, all fed by the active
workspace's facets (`WorkspaceFacets.h`) and actions. It also presents the
command palette (Ctrl+K) and the prompt queue's front question; every window
shows the same question, so it is answered wherever the person is. Under the
shell is the window's content surface (`WorkspaceWindow::Content`, drawn in
`kContentUiLayer` by a `UiRenderFeature` ordered before the shell's): the
active workspace shows its content there full bleed over the rect the shell
hands it (`IWorkspace::SetContentRect`: the window below the rail). The Level
draws its views as host images (`LevelViewSurface`), laid out by
`ViewportArrangement`; Materials, Animation and UI Preview show one
`presenters/ContentPicture` each (`docs/editor/interaction-grammar.md`). Every
surface a workspace or the shell shows is an authored document under
`editor/ui/`; nothing in `editor/` draws with ImGui, and the only ImGui in a
Kyusu window is the engine's diagnostics overlay (the console, on the backquote
key). The theme (`editor.ui.theme`, `theme.use.*`) is colours only:
`ThemeSheet` loads `editor/themes/<name>.json` into the palette and publishes
it as the stylesheet every document imports (`ui/AuthoredThemeStyleSheet`),
and `editor.ui.scale` sizes every document. The views meet at a divider the
person drags (`ViewDividerDrag`, kept in the `editor.view.divider_x/_y`
cvars); the shell's transitions follow `editor.ui.motion`.
Opening, closing and switching are requests applied at the frame boundary by
the one `WorkspaceTickSystem`, since a workspace adds and removes render
features as it comes and goes; each open workspace is ticked there too.

Data documents live once per process in the session's `DataDocumentStore`;
each workspace shows them through a `DataDocumentSet` view (its open documents
and active document), so the Data and Animation workspaces showing one file
edit one document, and the store closes it when its last view lets go. Both
present them through one `data/DataDocumentSheet`: the subtype registry, the
fields, the subtype's update, captured writes, event claims and previews, the
file path, the dial entries, and closing after asking.

Documents share one journal, the session's `DocumentSourceSet`: every
workspace's documents are sources in it (the level's is `LevelDocumentSource`,
one document for the whole world since a command may span zones). A file a
session holds and edits in place -- UI Preview's preview model, the animation
scenario -- is a `documents/SnapshotDocument`, journaled as snapshots of the
text it is saved as. Undo retakes
the newest step wherever it is and brings the owning workspace forward first;
a workspace's own staged edit, such as the level's live preview, is undone by
itself before that. A workspace with changed documents asks Save, Discard or
Keep editing before it closes, the exit prompt does the same for all of them,
and a source destroyed with changes is a debug assertion: whoever drops them
does so deliberately. Cook and Play save every document first and stop on a
conflict or a failure. Saving a document whose file changed on disk since it
was read is a conflict the author settles, never an overwrite.

A workspace is placed in a window. The main window is the engine's primary
presentation; `workspace.detach` (or a tab dragged down out of the rail)
opens another through `OpenPresentationWindow` with its own shell and the same
two `UiRenderFeature`s (content, then shell) scoped to that presentation, and
`IWorkspace::MoveTo` rebinds the workspace to the new `WorkspaceWindow`. UI
surfaces name their window, so each window draws and hit-tests only its own.
Each window shows the workspace most recently activated in it; closing a
window, or `workspace.attach`, brings its workspaces back, and a window left
empty closes. Platform events reach the shell and workspace of the window they
name.

Offscreen previews render only while something shows them: a workspace marks
what it displayed in a `DisplayLedger`, and the next frame renders and keeps
only that (`render/DisplayedTargets`; the level views read the same ledger). A
hidden workspace and a minimized window cost no target and no recording.

The level workspace (`level_editor/src/app/LevelWorkspace`) is the level
editor's composition root. Its constructor is the bring-up sequence, split
into named phases: `BuildDocument` -> `BuildPlayLoop` -> `BuildFileActions` ->
`OfferDocumentActions` -> `BuildSelectionSources` -> `BuildViewControls` ->
`BuildWheels` -> `BuildRouter` -> `BuildViewportRendering` -> `BuildUi`. It
composes; each responsibility has its own owner: the view arrangement
(`viewport/ViewArrangementSettings`), the world map (`workspace/WorldMapMode`),
the breadcrumb (`workspace/LevelBreadcrumb`), what the hands hold
(`workspace/HeldResources`), cooking and playing (`app/CookTransport`), a game
module's components (`app/ModuleComponents`) and the tools as actions
(`tools/ToolActions`). Member order is teardown order; the destructor
reproduces the load-bearing sequence explicitly.

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

Includes are src-root-relative (`shell/ShellWindow.h`, `brush/BrushMesh.h`).
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
   plus every layer-3 subsystem into the shared state the facets and tools read, and
   owns the mechanisms beside it: `WorkspaceInteractionRuntime` (the editing
   stack), `PendingBridgeEdit` and `PendingElementEdit` (staged previews),
   `SelectionActions` (verbs over the selection as a whole), `GridEditing`. It
   is the editor's central hub by design, so it has the widest fan-out; that
   breadth lives here, not scattered.
5. App composition: `app/`. `LevelWorkspace` owns the authoring hub, input,
   views, explorers and play loop, and wires them into the engine.

## Subsystem map

Shared shell (`editor/common/src/`):

| Directory | Owns | Extension seam |
| --- | --- | --- |
| `commands/` | Generic undo/redo infrastructure (`CommandStack`, `CompositeCommand`). | `ICommand` |
| `selection/` | Multi-element selection model (`SelectionService`, `SelectionContext`, `SelectableRef`). | -- |
| `tools/` | Tool framework (`ToolRegistry`, `ToolContext`). | `ITool` |
| `interaction/` | Drag-interaction host (`InteractionHost`). | `IInteraction` |
| `input/` | Generic input primitives (`InputRouter` handler chain + pointer capture, `KeymapFile`). | router handlers |
| `properties/` | The toolkit-free property model: `PropertyNode` trees (key, descriptor, value as text, children; no rows), `IPropertySource` (begin/preview/commit/cancel mapped onto a document's own transaction) and `PropertyEditSession` (the one edit in flight, ended by any interruption). The level's source is `authoring/ComponentPropertySource`. | inspectors, cards, drawers |
| `observers/` | `WeakObserverList`: subscribers held for as long as they keep their token. | selection, resources, problems |
| `resources/` | `ActiveResources`: what the person is holding, per asset kind, with a stash; changes reach the tool in hand through `ToolRegistry`. `ResourceActions` offers the stash as actions. | tools, workspaces |
| `problems/`, `notices/` | `ProblemList` (every located problem, republished whole by each source) and `NoticeQueue` (brief word that something happened). | the shell |
| `prompts/` | `PromptQueue` (questions that hold an operation, answered one at a time; an `Asker` withdraws its questions when it goes) and the document questions over it: `AskAboutChanges`, `AskAboutDocuments` (the one prompt listing the changed documents a close would drop), `DocumentExitHold` (exit waits on changed documents) and `DocumentSaveFollowUp` (a failed save is a problem, a conflict is a question). | workspaces, Kyusu |
| `presenters/` | Neutral models as what a document shows: `PropertySheet` turns property nodes into rows and a row's edit back into its source; `ContentPicture` shows a host image full bleed with picked marks and frames; `PlanePad` shows a 2D value as a pad the workspace drags; `StageOrbit` turns a press on a stage into a click or turntable orbit; `ColorPicker` edits a colour property on a hue and saturation wheel with a value bar, through the property's edit session. Used by the shell and by documents a workspace owns. | shell, workspaces |
| `shell/` | The replaceable presentation policy's home (`docs/editor/interaction-grammar.md`); today `ShellWindow`, `TimelineDeck`, `CommandPalette`, `PromptDialog`, `ThemeSheet`, `SurfacePlacement` (the explorer opens in free space, the selection at its place; both moved by the header and sized by the corner), `TransientSurfaces`, the generic open/dismiss/Escape mechanism its popovers, drawers and explorers are presets of, and the `Shelf` (the project's content along a window's bottom, or alone in a window of its own: tiles to open, carry and drop, its file verbs, a tile's wheel and `NewContentWheel`). Workspaces never include it. | Kyusu |
| `actions/` | The one table of operations (`ActionCatalog`: id, label, category, chords, no callbacks) and who performs them (`ActionContext` handlers, resolved per window by `ActionContextStack`, closest context first); `ActionWheelModel` offers a context's actions as a wheel. | Kyusu, workspaces |
| `ui/` | The theme palette (`EditorUiStyle`, loaded from `editor/themes/*.json` by `EditorThemeFile`) and its stylesheet (`AuthoredThemeStyleSheet`), the display scale choice (`UiScaleChoice`), material thumbnails as host images, and the wheel's screen (`RadialMenuScreen`). | a palette entry; an SVG in `editor/ui/icons/` |
| `icons/` | `IconId`, the leaf enum a tool or control names an icon by. | -- |
| `workspaces/` | The workspace model: `IWorkspace`, the `WorkspaceKind` row, `WorkspaceHost` (open, close, activate, window placement, requests at the frame boundary; headless-tested), and `ExplorerTable` (a workspace's explorers as rows: each its action, its entry and its paging and choosing). | a row in `WorkspaceKinds.cpp` |
| `render/` | Display-driven offscreen target lifetime (`DisplayLedger`, `DisplayedTargets`), publishing a target as a host image (`TargetImagePublish`), a workspace's feature attached and detached (`AttachRenderFeature`, `DetachRenderFeature`), the overlay pipelines every workspace draws with (`EditorImmediatePipeline`, `EditorWideLinePipeline` and its `EditorLineSegment`), `DetachRenderFeature`. | -- |
| `viewport/` | `ViewportId`. | -- |
| `project/` | Project descriptor + resolution + mounting + spawning (`Project`, `ProjectArgs`, `ProjectContentMount`, `ProcessLaunch`, `MaterialLibrary`); the project's files as content (`ProjectFiles`, `AssetQuery`, `ReferenceIndex`), every file stating its id (`AssetAdoption`), and moving, copying, trashing and importing them as journal steps (`ProjectFileSteps`, which open documents follow). | a step kind in `ProjectFileSteps` |

Level editor (`editor/level_editor/src/`):

| Directory | Owns | Extension seam |
| --- | --- | --- |
| `app/` | The composition root (`LevelWorkspace`) and `EditorCookRuntime` (the cook session, the player it feeds, and the serials that hand one to the other). | -- |
| `workspace/` | The per-document authoring hub (`EditorWorkspace`, `BrushManipulationSink`) plus the mechanisms it composes: `WorkspaceInteractionRuntime`, `PendingBridgeEdit`, `PendingElementEdit`, `SelectionActions`, `GridEditing`, `EscapePolicy`. | -- |
| `brush/` | Half-edge brush geometry kernel: mesh, ops, tessellation, validation. Pure leaf (engine-only), consumed by `document`, `meshedit`, `render`, `editmodes`, interactions, and the test suite. | -- |
| `input/` | Viewport-coupled input (`ViewportNavigation`, `ViewportToolDispatcher`, `SdlEventTranslation`). | -- |
| `editmodes/` | Transform gizmos and manipulator sessions (`TranslateManipulator`, `BoundsManipulator`, `EditSessionHost`). | manipulators |
| `meshedit/` | Polygon mesh-editing verbs (`MeshEditService`). | `IMeshEditTarget` |
| `viewport/` | Viewport layout, camera, picking (`ViewportLayout`, `EditorCamera`, `EditorViewportCameraSystem`, `Picking`). | -- |
| `render/` | Viewport render features and pipelines (`EditorRenderFeature`, grid/gizmo/selection/solid passes, the 14 embedded shaders). | `IRenderFeature` (engine) |
| `ui/` | The level editor's authored surfaces: the views (`LevelViewSurface`) and the cook profiles dialog (`CookProfilesModal`). | -- |
| `document/` | Scene/document domain (see below). | -- |
| `project/` | Play-In-Editor (`PieDriver`, `PieSession`). | -- |

Materials workspace (`editor/material_editor/src/`, flat): `MaterialWorkspace`,
`MaterialDocumentSet` (the open materials as journal documents, one undo
history each, headless-tested), `MaterialEditSession`
(open/edit/save/duplicate, conflict detection by file baseline),
`EditMaterialCommand`, `PreviewPrimitives` (procedural sphere/cube/plane),
`MaterialPreviewRenderFeature` (MeshForwardPass into its own offscreen
target, shown full-bleed by a `presenters/ContentPicture` host image and
turned over by a drag on it), `MaterialProperties` (the open material's
parameters as a property source on the card: a drag previews, letting go is
one step), and a texture opened from the Shelf, which puts its import
settings on the card (`TextureImportProperties`: each change saves the sidecar
and recooks, and the cook is read back). Materials and textures are found on
the Shelf; `project/MaterialLibraryActions` holds the M and rescan actions the
Level shares. Every open material is on the
shell's tabs (`WorkspaceDocumentFacet::Open`,
`IWorkspace::ShowDocument`). Live preview pushes
the working description into the resident material via `AssetSystem::Reload`;
a watched `.smat` that changes on disk is taken by a clean document and kept
out of the stack while a document holds its own changes (`DocumentSource::FileChangedOnDisk`
behind `SourceReloadRoots::SetReloadFilter`). What an operation could not do is
one of the workspace's problems.

Data workspace (`editor/data_editor/src/`): `DataWorkspace` presents the
active document whole, as a sheet (`WorkspaceSelectionFacet::Primary`): its
fields laid out by the schema through `data/DataProperties` (a property per
JSON path; array and record verbs; validation errors as notes beside their
field and as problems), or its JSON through `DataSourceText` (View Source).
A new asset is a `DataCreation` card; files move by their path property
(`properties/FilePathProperty`, shared with Materials). A subtype makes its
documents read as what they are through `data/DataSubtypeAuthoring`: it
reshapes the laid-out properties, takes the writes, candidates and verbs whose
meaning it knows (`Perform` for a verb that changes the data, as one step),
claims platform events, and may add preview sections. Kyusu composes every
workspace library's authorings into one `DataSubtypeAuthoringSetup`, and each
workspace that shows data documents fills its own registry from it, so an
asset reads the same in Data as in Animation. The
input action set and profile (`input/Input*Authoring`) are built this way:
titled cards, actions chosen among the declared ones they can drive, a
binding's shape setting its slots, and a slot picked or filled by pressing a
control. The movement profile (`movement/MovementProfileAuthoring`) titles
its rules and marks each live or off in a situation the author dials, with
what that situation resolves to and how the profile feels as sections beside
it (`MovementPreviewProperties`). Selectors and slot maps
(`animation_editor` `authoring/AnimationPredicateAuthoring`) title each
condition row as it reads, add tests of the facts and requests the project's
schemas declare, widen a row with an "or", and choose facts, intents and
parameters among the declared ones. A control whose value moved while its rows
were laid out is not a change (`PropertySheet` settles for one presentation).

Project workspace (`editor/project_browser/src/`, flat): `ProjectWorkspace`,
`ProjectCatalog` (recent projects JSON in the user config dir,
headless-tested), `ProjectRack` (the catalog as the workspace's content, a
rack of cartridges behind the shell), `ProjectSettings` and `ProjectCreation`
(the picked project's settings, each change saved at once, and a new project
as property sources the shell's card presents), `ProjectRelaunch` (the command
that opens a project in a new process).

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
- No file under `editor/` names ImGui. Geometry, input, tools and viewport
  math are written in engine vectors (`Vec2d`), and everything a person sees
  is an authored document (`docs/editor/interaction-grammar.md`).
- A workspace never asks whether the window's UI has the pointer before
  acting on a press or a move: whatever reaches it is a gesture the UI did not
  take, because the engine's router gives each gesture to whoever took its
  press (`docs/ui/architecture.md`, Input). A press a document began and the
  shell carries on, like a number scrub, is handed over through
  `PlatformEventRouter::HandGestureToApplication`.

Enforced by `scripts/check_editor_layering.sh` (a ctest) plus the include-path
firewall above.

## Where do I add ...

- Something a person reads or edits: a property source (`IPropertySource`)
  the workspace's selection facet hands the shell as a section, which the
  card and drawer present through `PropertySheet`. A data subtype that needs
  more than its schema registers an `IDataSubtypeAuthoring`. A purpose-built
  surface (a pad, a dialog) is an authored document under `editor/ui/` the
  workspace asks the window for by role, presenting its rows through
  `presenters/`. Nothing remembers where it was: settings are archived cvars.
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
  `ActInspector`). The Level's selection is `OnRequest`: it is worked through
  the context wheel (Space) -- a face's material is an asset property there,
  offered from what is to hand -- and the shell shows it in full only when
  asked for (`selection.details`) or while an edit is staged. A workspace
  whose properties are its content keeps the card shown while something is
  selected. What a workspace lets a person browse is
  an explorer (`WorkspaceExplorerInfo`, paged through `Explore` and chosen
  from with `ChooseInExplorer`; Animation and UI Preview declare theirs as
  `ExplorerTable` rows); the Level's materials are
  `workspace/MaterialExplorer`, which the shell shows as a tray and opens from
  its `explorer.materials` action or the cartridge; its prefabs are
  `workspace/PrefabExplorer` (Shift+P), whose choice takes up the Entity tool,
  and its structure is one outline, `workspace/HierarchyOutline` (Tab): the
  world, its graphs and zones (`workspace/PartitionOutline`) with the edited
  zone's entities (`workspace/WorldExplorer`) under it, and a level that is
  not a world is its entities alone. The structural verbs (parent, hide, lock,
  move to a zone, a placement's source) are `workspace/EntityVerbs`; a picked
  zone, graph or world is the selection card when no entity is selected
  (manifest edits, zone load and focus, streaming preview). The partition
  also turns the world's validation into problems, and
  `IWorkspace::RevealProblem` answers the shell's problems popover by picking
  the problem's zone or graph. `IWorkspace::Location` says where the work is
  -- the world, the graph and zone the camera is in, and the selection -- and
  the shell shows it as a breadcrumb whose parts reveal their rows in the
  hierarchy (`ExplorerChoice::Reveal`). Its MAP part (`view.map`) shows the world
  from above in place of the views (`document/WorldMap` lays the zones out
  as plates at their footprint with the docks and links between them;
  `ui/WorldMapSurface` shows it): the zone being edited, the open ones, the
  one the camera is in and the ones with problems are marked, and a click on
  a zone edits it with the camera framed on it.
  `workspace/LightingDiagnostics` says how the lighting is doing where it
  applies: a selected light's shadow tier, slot and pool on its card, what a
  baked light or probe volume became at the last cook, unshadowed lights and
  a stale baked preview as problems, and `view.baked_lighting` /
  `view.rerender_shadows` as view actions. F (`view.frame_selection`) frames
  what is selected through `viewport/CameraFraming`. `view.zone_graph` draws a
  world's docks and links between its zones' centres over the views
  (`document/ZoneGraph`, `render/ZoneGraphRenderer`), coloured by validation.
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
  hot and the station shows the active one; neither learns the type behind them.
- An undo-able edit: implement `ICommand` next to its domain, run it through the
  `CommandStack`.
- An operation a person can invoke: define it in the `ActionCatalog` (its id,
  label, category and default chords -- data, no callback) and claim it in the
  `ActionContext` of whatever performs it (`OfferAction` does both). The level
  workspace's own are offered in `LevelWorkspace::OfferDocumentActions` and by
  each verb source (`ElementVerbs`, `EntityVerbs`, `FaceUvSource`,
  `ManipulationActions`); the operations every workspace shares (undo, redo, save all) are Kyusu's, in
  `KyusuApp::DefineApplicationActions`, and Save is each workspace's own
  (`SaveAction()`). Kyusu puts the active workspace's context in play in its
  window, so a chord, the hatch, the console (every action is a console
  command of its id) and the palette all reach one table, and a chord belongs to one action whoever
  performs it. Tool activation is generated from the registry, under
  `tool.<id>` (`OfferToolActions`). Any action is rebindable from `keybinds.json`. A held key is
  owned by a `RadialMenuSession` rather than an action handler, which fires on
  presses; its chord is still the catalog's (`tool.wheel`, `gizmo.wheel`). There is
  one radial-menu mechanism (`RadialMenuMath`, `RadialMenuSession`, `FaceOf`
  for what an open wheel shows, and `RadialMenuScreen`, which draws it on the
  window's overlay surface) over `IRadialMenuModel`; the tools are one model
  (`tool.wheel`, `ToolRegistryMenuModel`), the gizmo modes another
  (`gizmo.wheel`, `TransformModeMenuModel` over the `TransformModeItems`
  table the Transform actions read too), and what the pointer is on a third
  (`context.wheel`, Space, `actions/ContextMenuModel`): as it opens the
  workspace aims at what is under the pointer (`LevelWorkspace::AimAt` selects
  it), and the wheel offers that selection's choices as fans with the one in
  effect marked, its enabled actions, and More... (`selection.details`, the
  full sheet), with its values read out in the hub (`IRadialMenuModel::Summary`).
  A wheel of words is sized by `RadialMenu::Proportions`. An open wheel is
  modal, which is what keeps the others closed. A wheel may instead open on a
  click and be chosen from by the next one (`RadialMenuGesture::Clicked`): the
  shell runs the Shelf's tile and NEW wheels so, each on a router of its own.
  A wheel's screen is open only while its wheel is up.
- A handle on the object: a row in `workspace/PropertyHandles`'s rule table
  (component, field, kind). A Radius is a ring facing each view with a knob to
  drag; a Scroll changes its number while the wheel turns over the object.
  Both edit through the component property source, so a drag previews live and
  commits as one step and every interruption cancels it; the marks go in the
  overlay's `Handles` layer, which no tool writes.
- A viewport visual: a render feature/pass in `level_editor/src/render/`, added in
  `LevelWorkspace::BuildViewportRendering`. A tool's world strokes go through
  `EditorOverlayState` (segments, dials, the drag readout), drawn into each view
  by `AnnotationStrokeRenderer`; what sits at a point (labels, handles, buttons,
  a dial's knob) becomes a `ViewMark` the views document places.
- A scripted walkthrough: `input.pointer`, `input.key`, `input.button` and
  `input.drop` push platform events at a named frame, so an unattended run can
  hold a wheel open, click a view or drop a file before `render.screenshot`
  (or `editor.window.screenshot`) captures it.
- What a file carried off the Shelf means over a workspace's content:
  `IWorkspace::DropAsset`. What it can make new: its kind's `Creates`, the
  same definitions its workspace offers.
- A tunable: a cvar registered where it is read (see `editor.cull_backfaces` in
  `BuildViewportRendering`).
- A workspace: a library under `editor/<name>/` linking `editor_common`, with
  an `IWorkspace` that exposes the facets it has (documents, operation,
  selection, resources, explorers, timeline, view) and shows its content on
  the window's content surface, and one row in
  `editor/kyusu/src/app/WorkspaceKinds.cpp` that constructs it with exactly
  the services it needs. It never includes another workspace's headers or the
  shell.
- An instrument or a placement rule: the shell's (`common/src/shell/` and
  `editor/ui/shell.rml`), fed by a facet. A workspace never names a screen
  position.
