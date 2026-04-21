# Sencha Editor — Crude Level Designer Architecture Spec

Target: a single executable implementing a Hammer-style 4-way viewport level designer.
This document is an execution spec. Implement it phase by phase in order.

---

## Constraints and Conventions

- All classes live in the global namespace. No enclosing namespace.
- Class names: `PascalCase`. File names: `PascalCase.h` / `PascalCase.cpp`.
- Member variables: `PascalCase`. Methods: `PascalCase`. No `m_` prefix.
- Interface classes: `I` prefix (`ITool`, `ICommand`, `IEditorPanel`).
- Follow all naming patterns present in the engine (`SdlWindow`, `EntityRegistry`, `VulkanBootstrap`).
- The editor has a hard build-time dependency on `SENCHA_ENABLE_VULKAN` and `SENCHA_ENABLE_DEBUG_UI`.
- The engine must not gain any dependency on editor code.

---

## Build Topology

Add `editor/` as a sibling to `engine/`, `app/`, `example/`, `test/`.

In the root `CMakeLists.txt`, add:
```cmake
add_subdirectory(editor)
```

`editor/CMakeLists.txt`:
```cmake
add_executable(sencha_editor
    main.cpp
    # glob all .cpp under app/ document/ commands/ selection/ tools/
    # viewport/ render/ ui/ level/ level/tools/
)

target_link_libraries(sencha_editor PRIVATE sencha::engine)

target_compile_definitions(sencha_editor PRIVATE
    SENCHA_ENABLE_VULKAN
    SENCHA_ENABLE_DEBUG_UI
)
```

Use explicit source lists or glob-recurse. One executable, no sub-targets.

---

## Engine Additions (Phase 1)

These belong in the engine, not the editor. Add them beside the existing `Grid2d.h`.

### `engine/include/math/spatial/GridPlane.h`

Header-only. No namespace.

```cpp
struct GridPlane {
    Vec3d    Origin       = {};
    Vec3d    AxisU        = { 1, 0, 0 };  // normalized
    Vec3d    AxisV        = { 0, 0, 1 };  // normalized
    float    Spacing      = 1.0f;         // major cell size in world units
    uint32_t Subdivisions = 10;           // minor lines per major cell

    Vec3d Snap(Vec3d worldPos) const;
    Vec3d Project(Vec3d worldPos) const;  // project onto plane, return closest point
};

namespace GridPlanes {
    inline GridPlane XZ(float spacing = 1.0f);  // ground plane, Y=0
    inline GridPlane XY(float spacing = 1.0f);  // front plane,  Z=0
    inline GridPlane YZ(float spacing = 1.0f);  // side plane,   X=0
}
```

`Snap`: project world point onto plane, round to nearest `Spacing` on each axis, return 3D point.
`Project`: dot product against AxisU and AxisV to find nearest on-plane point.

### `engine/include/math/spatial/Grid3d.h`

Header-only. Direct 3D analog of `Grid2d<T>`. Not used in the crude editor milestone but placed now.

```cpp
template<typename T>
struct Grid3d {
    uint32_t       Width  = 0;
    uint32_t       Height = 0;
    uint32_t       Depth  = 0;
    std::vector<T> Cells;

    bool     IsEmpty()  const { return Cells.empty(); }
    uint32_t Count()    const { return Width * Height * Depth; }
    bool     InBounds(uint32_t col, uint32_t row, uint32_t layer) const;
    T&       Get(uint32_t col, uint32_t row, uint32_t layer);
    const T& Get(uint32_t col, uint32_t row, uint32_t layer) const;
    void     Set(uint32_t col, uint32_t row, uint32_t layer, const T& value);
};
```

Index formula: `layer * Width * Height + row * Width + col`.

---

## Folder and File Topology

```
editor/
  CMakeLists.txt
  main.cpp

  app/
    EditorApp.h
    EditorApp.cpp

  project/
    EditorProject.h          // POD: project name, root path (stub)

  document/
    IEditorDocument.h        // stable seam
    DocumentRegistry.h
    DocumentRegistry.cpp

  commands/
    ICommand.h               // stable seam
    CommandStack.h
    CommandStack.cpp

  selection/
    SelectableRef.h
    ISelectionContext.h      // stable seam
    SelectionContext.h
    SelectionContext.cpp
    SelectionService.h
    SelectionService.cpp

  tools/
    ITool.h                  // stable seam
    ToolContext.h
    ToolRegistry.h
    ToolRegistry.cpp

  viewport/
    EditorViewport.h
    EditorViewport.cpp
    IViewportLayout.h        // stable seam
    FourWayViewportLayout.h
    FourWayViewportLayout.cpp
    EditorCamera.h
    EditorCamera.cpp
    ViewportInput.h
    ViewportInput.cpp
    Picking.h
    Picking.cpp

  render/
    EditorRenderFeature.h
    EditorRenderFeature.cpp
    GridRenderer.h
    GridRenderer.cpp
    WireframeRenderer.h
    WireframeRenderer.cpp
    SelectionRenderer.h
    SelectionRenderer.cpp

  ui/
    EditorUiFeature.h        // IRenderFeature: ImGui host
    EditorUiFeature.cpp
    IEditorPanel.h
    ViewportPanel.h
    ViewportPanel.cpp
    ToolPalettePanel.h
    ToolPalettePanel.cpp
    SceneHierarchyPanel.h
    SceneHierarchyPanel.cpp
    InspectorPanel.h
    InspectorPanel.cpp

  level/
    LevelWorkspace.h
    LevelWorkspace.cpp
    LevelDocument.h
    LevelDocument.cpp
    LevelScene.h
    LevelScene.cpp
    LevelCommands.h
    LevelCommands.cpp
    LevelSerialization.h
    LevelSerialization.cpp

    tools/
      SelectTool.h
      SelectTool.cpp
      CubeTool.h
      CubeTool.cpp
      CameraTool.h
      CameraTool.cpp
```

---

## Stable Seams (Interfaces)

Only these five are abstract. Everything else is concrete.

### `ICommand` — `commands/ICommand.h`
```cpp
struct ICommand {
    virtual void Execute() = 0;
    virtual void Undo()    = 0;
    virtual ~ICommand()    = default;
};
```

### `IEditorDocument` — `document/IEditorDocument.h`
```cpp
struct IEditorDocument {
    virtual std::string_view GetDisplayName()             const = 0;
    virtual bool             IsDirty()                    const = 0;
    virtual bool             Save()                             = 0;
    virtual bool             Load(std::string_view path)       = 0;
    virtual ~IEditorDocument()                                  = default;
};
```

### `ISelectionContext` — `selection/ISelectionContext.h`
```cpp
struct ISelectionContext {
    virtual std::string_view         GetContextName() const = 0;
    virtual std::span<SelectableRef> GetSelected()    const = 0;
    virtual void SetSelection(std::span<SelectableRef> refs) = 0;
    virtual void ClearSelection()                            = 0;
    virtual ~ISelectionContext()                             = default;
};
```

### `ITool` — `tools/ITool.h`
```cpp
struct ITool {
    virtual std::string_view GetName()                                              const = 0;
    virtual void             OnActivate(ToolContext&)                                     = 0;
    virtual void             OnDeactivate()                                               = 0;
    virtual void             OnViewportInput(const ViewportInputEvent&, ToolContext&)     = 0;
    virtual ~ITool()                                                                      = default;
};
```

### `IViewportLayout` — `viewport/IViewportLayout.h`
```cpp
struct IViewportLayout {
    virtual std::span<EditorViewport> GetViewports()                  = 0;
    virtual EditorViewport*           GetActiveViewport()             = 0;
    virtual void                      OnResize(uint32_t w, uint32_t h) = 0;
    virtual ~IViewportLayout()                                         = default;
};
```

### `IEditorPanel` — `ui/IEditorPanel.h`
```cpp
struct IEditorPanel {
    virtual std::string_view GetTitle()  const = 0;
    virtual bool             IsVisible() const = 0;
    virtual void             OnDraw()          = 0;
    virtual ~IEditorPanel()                    = default;
};
```

---

## Dependency Rules

```
app/        → may compose everything
level/      → document, selection, commands, tools, viewport
viewport/   → selection (read-only queries), tools (input dispatch only)
ui/         → reads editor state via queries, dispatches commands only — never mutates
render/     → engine render services, viewport state, selection state (read-only)
selection/  → no level dependency
commands/   → no level dependency
tools/      → no level dependency
document/   → no level dependency
engine/     → no editor dependency (never include editor headers)
```

---

## Registry Mutation Chain

`LevelScene` is the **only** code that calls `Registry` mutation APIs directly.

```
ITool::OnViewportInput()
  → ToolContext.Commands.Execute(make_unique<CreateCubeCommand>(pos, scene, document))

CreateCubeCommand::Execute()
  → Scene.CreateCube(pos)    // LevelScene — sole Registry mutation point
  → Document.MarkDirty()     // LevelDocument — dirty state tracking

LevelScene::CreateCube(pos)
  → Registry.Entities.Create()
  → Registry.Components.Get<TransformStore<Transform3f>>().Add(entity, ...)
  → Registry.Components.Get<SparseSetStore<CubePrimitive>>().Add(entity, ...)
```

Access rules per layer:

| Layer | Permitted | Forbidden |
|---|---|---|
| UI panels | `CommandStack::Execute`, read-only `LevelScene` queries | Registry, `LevelScene` mutations |
| Tools | `CommandStack::Execute`, `PickingService`, read `ISelectionContext` | Registry, `LevelScene`, `LevelDocument` directly |
| Commands | `LevelScene` mutation methods, `LevelDocument::MarkDirty` | Registry directly |
| `LevelScene` | `Registry` freely | Editor UI, tools |
| `LevelDocument` | `LevelScene`, `Registry` (save/load paths only) | Editor UI, tools |

`LevelScene` also exposes **read-only query methods** callable by UI and tools without going through commands (`GetEntityCount`, `GetAllEntities`, `HasEntity`, etc.).

---

## Entry Point

`main.cpp`:
```cpp
int main() {
    Application app;
    return app.Run<EditorApp>();
}
```

`EditorApp : Game` — overrides:
- `OnConfigure` — set window title, disable vsync or configure as needed
- `OnStart` — construct `LevelWorkspace`; register render features; add UI panels
- `OnRegisterSystems` — register `EditorCameraSystem` into `FrameUpdate` phase
- `OnPlatformEvent` — forward SDL events to active viewport input handler
- `OnShutdown` — teardown in reverse construction order

`EditorApp` owns:
```cpp
CommandStack            Commands;
SelectionService        Selection;
DocumentRegistry        Documents;
unique_ptr<LevelWorkspace> ActiveWorkspace;
```

`OnStart` installs render features in this order:
```cpp
auto& renderer = services.Get<Renderer>();
renderer.AddFeature<MeshRenderFeature>();    // engine runtime mesh (existing)
renderer.AddFeature<EditorRenderFeature>(); // grid + wireframe + selection
renderer.AddFeature<EditorUiFeature>();     // ImGui host + all editor panels
```

`ImGuiDebugOverlay` is **not** registered. `EditorUiFeature` owns the full ImGui lifecycle.

---

## ImGui Integration — `EditorUiFeature`

`EditorUiFeature : IRenderFeature`. Not `ImGuiDebugOverlay`. Not `IDebugPanel`.

Responsibilities:
- `Setup(RendererServices&)` — initialize ImGui Vulkan + SDL3 backends
- `OnDraw(FrameContext&)` — `ImGui::NewFrame()`, draw docking space, menu bar, call `OnDraw()` on each `IEditorPanel`, `ImGui::Render()`, submit draw data via Vulkan backend
- `Teardown()` — shut down ImGui backends

Owns `std::vector<unique_ptr<IEditorPanel>> Panels`. Panels added by `EditorApp::OnStart`.

Menu bar: "File" (New, Open, Save, Save As, Exit). Placeholder implementations for Phase 1.

---

## Viewport System

### `EditorCamera` — `viewport/EditorCamera.h`

Two modes, one struct:
```cpp
struct EditorCamera {
    enum class Mode { Perspective, Orthographic };
    Mode    ActiveMode  = Mode::Perspective;

    // Perspective state
    Vec3d   Position    = { 0, 2, -5 };
    float   Yaw         = 0.0f;    // degrees
    float   Pitch       = 0.0f;    // degrees, clamped ±89
    float   FovY        = 60.0f;
    float   MoveSpeed   = 5.0f;

    // Orthographic state
    float   OrthoHeight = 20.0f;
    Vec3d   OrthoCenter = {};
    Vec3d   OrthoAxis   = { 0, 1, 0 }; // axis pointing at viewer

    float   Near = 0.01f;
    float   Far  = 10000.0f;

    CameraRenderData BuildRenderData(float aspectRatio) const;
};
```

Perspective controls (while right mouse button held):
- Mouse delta → yaw/pitch
- WASD → strafe/forward/backward relative to look direction
- Q/E → down/up in world space

Orthographic cameras are fixed in the crude milestone. No pan or zoom yet.

### `EditorViewport` — `viewport/EditorViewport.h`

```cpp
struct EditorViewport {
    EditorCamera     Camera;
    GridPlane        ActiveGrid;    // derived from camera mode/axis
    ImVec2           RegionMin;     // ImGui panel screen position
    ImVec2           RegionMax;
    bool             IsActive = false;

    float            AspectRatio() const;
    CameraRenderData BuildRenderData() const;
};
```

### `FourWayViewportLayout` — `viewport/FourWayViewportLayout.h`

Concrete, not virtual internally. Implements `IViewportLayout`.

Layout assignment (fixed):
```
[ 0: Perspective  | 1: Top   (XZ, GridPlanes::XZ()) ]
[ 2: Front (XY)   | 3: Side  (YZ, GridPlanes::YZ()) ]
```

```cpp
class FourWayViewportLayout : public IViewportLayout {
public:
    EditorViewport Viewports[4];
    int            ActiveIndex = 0;

    std::span<EditorViewport> GetViewports()              override;
    EditorViewport*           GetActiveViewport()         override;
    void                      OnResize(uint32_t, uint32_t) override;
};
```

`OnResize` recalculates `RegionMin`/`RegionMax` for each quadrant.

### `ViewportPanel` — `ui/ViewportPanel.h`

`ViewportPanel : IEditorPanel`. Draws four `ImGui::BeginChild` regions. Forwards mouse/keyboard events from each hovered region into the corresponding `EditorViewport`. Dispatches `ViewportInputEvent` to `EditorApp` → active tool.

---

## Picking — `viewport/Picking.h`

```cpp
struct PickResult {
    EntityId Entity;
    float    Distance;
    Vec3d    HitPoint;
};

class PickingService {
public:
    std::optional<PickResult> Pick(
        const Ray3d&    ray,
        const Registry& registry,
        float           maxDistance = 10000.0f
    ) const;
};
```

Implementation: iterate entities with `Transform3f` + `CubePrimitive`. Build AABB from `HalfExtents` + world position. Ray/AABB intersection, return nearest hit.

`SelectTool` calls `PickingService::Pick`, then executes `SelectCommand`. The tool never touches `ISelectionContext` directly.

Future GPU picking: replace `Pick()` implementation. `SelectTool` is unchanged.

---

## Tool System

### `ToolContext` — `tools/ToolContext.h`

```cpp
struct ToolContext {
    CommandStack&     Commands;
    ISelectionContext& Selection;
    PickingService&    Picking;
    LevelScene&        Scene;         // read-only queries only from tools
};
```

Tools receive `ToolContext&` in `OnActivate` and store a pointer. They do not store `LevelScene&` for mutation — mutation happens only through commands they construct.

### `ToolRegistry` — `tools/ToolRegistry.h`

```cpp
class ToolRegistry {
public:
    void     Register(unique_ptr<ITool> tool);
    void     Activate(std::string_view name, ToolContext& ctx);
    ITool*   GetActive();
    std::span<unique_ptr<ITool>> GetAll();
};
```

`LevelWorkspace` owns one `ToolRegistry` and registers `SelectTool`, `CubeTool`, `CameraTool`.

---

## Selection System

### `SelectableRef` — `selection/SelectableRef.h`

```cpp
struct SelectableRef {
    EntityId Entity;
    // extend with registry tag when multi-registry selection is needed
};
```

### `SelectionContext` — concrete `ISelectionContext` impl

One instance per workspace. `LevelWorkspace` owns `SelectionContext LevelSelection`.

### `SelectionService` — `selection/SelectionService.h`

Named registry of `ISelectionContext*`. For future: asset browser context, material graph context, etc.

---

## Command Stack — `commands/CommandStack.h`

```cpp
class CommandStack {
public:
    void Execute(unique_ptr<ICommand> cmd);  // runs cmd->Execute(), pushes to history
    void Undo();
    void Redo();
    bool CanUndo() const;
    bool CanRedo() const;
    void Clear();
};
```

Linear history. No tree. `Execute` clears redo stack.

---

## Level System

### `CubePrimitive` — defined in `level/LevelScene.h`

```cpp
struct CubePrimitive {
    Vec3d HalfExtents = { 0.5f, 0.5f, 0.5f };
};
```

Explicitly a temporary editor primitive. Register `TypeSchema<CubePrimitive>` with a `HalfExtents` field. Register `ComponentSerializer<CubePrimitive>` with JSON key `"cube_primitive"` and FourCC `CUBE`.

Never referenced by engine code. Lives entirely in `editor/level/`.

### `LevelScene` — `level/LevelScene.h`

Single access point for Registry mutations. Also provides read-only queries.

```cpp
class LevelScene {
public:
    explicit LevelScene(Registry& registry);

    // Mutations (called by commands only)
    EntityId CreateCube(Vec3d position, Vec3d halfExtents = { 0.5f, 0.5f, 0.5f });
    EntityId CreateCamera(Vec3d position);
    void     DestroyEntity(EntityId entity);
    void     SetTransform(EntityId entity, const Transform3f& transform);

    // Read-only queries (callable by UI and tools)
    bool                          HasEntity(EntityId entity)  const;
    uint32_t                      GetEntityCount()            const;
    std::span<const EntityId>     GetAllEntities()            const;
    const Transform3f*            TryGetTransform(EntityId)   const;
    const CubePrimitive*          TryGetCube(EntityId)        const;
    const CameraComponent*        TryGetCamera(EntityId)      const;

private:
    Registry& Registry_;
};
```

### `LevelDocument` — `level/LevelDocument.h`

`IEditorDocument` implementation. Owns or borrows the `Registry`.

```cpp
class LevelDocument : public IEditorDocument {
public:
    std::string_view GetDisplayName() const override;
    bool             IsDirty()        const override;
    bool             Save()                 override;
    bool             Load(std::string_view path) override;

    void MarkDirty();
    LevelScene& GetScene();

private:
    std::string FilePath_;
    bool        Dirty_ = false;
    Registry    Registry_;    // owned; editor uses its own registry separate from ZoneRuntime
    LevelScene  Scene_;
};
```

Note: the level editor uses its own `Registry` instance, not the game's `ZoneRuntime`. `ZoneRuntime` is the runtime authority. `LevelDocument` owns the editor scene state. When the game eventually runs an edited level, it loads from the saved file.

### `LevelWorkspace` — `level/LevelWorkspace.h`

```cpp
class LevelWorkspace {
public:
    void Init(CommandStack& commands, SelectionService& selectionService);
    void OnInput(const ViewportInputEvent& e);
    void OnRender(EditorRenderContext& ctx);

    LevelDocument          Document;
    FourWayViewportLayout  Layout;
    SelectionContext        LevelSelection;
    ToolRegistry           Tools;
    ToolContext            ActiveToolContext();  // builds ToolContext from owned state
};
```

`LevelWorkspace` registers these tools at init (in order, first is default active):
1. `SelectTool`
2. `CubeTool`
3. `CameraTool`

### `LevelCommands` — `level/LevelCommands.h`

Concrete command types. All follow the same pattern:

```cpp
struct CreateCubeCommand : ICommand {
    Vec3d        Position;
    LevelScene&  Scene;
    LevelDocument& Document;
    EntityId     CreatedEntity = {};  // filled by Execute, used by Undo

    void Execute() override;
    void Undo()    override;
};

struct CreateCameraCommand : ICommand { /* same pattern */ };

struct DeleteEntityCommand : ICommand {
    // stores full component snapshot for undo
};

struct SelectCommand : ICommand {
    std::vector<SelectableRef> NewSelection;
    std::vector<SelectableRef> PreviousSelection;
    ISelectionContext&         Context;

    void Execute() override;
    void Undo()    override;
};
```

---

## Render Path

### Feature order (guaranteed by insertion order into `Renderer`)

1. `MeshRenderFeature` — runtime mesh, `RenderPhase::MainColor`
2. `EditorRenderFeature` — overlays, `RenderPhase::MainColor`
3. `EditorUiFeature` — ImGui, `RenderPhase::MainColor`

### `EditorRenderFeature::OnDraw`

For each `EditorViewport` in the active layout:
1. Set `vkCmdSetScissor` + `vkCmdSetViewport` to viewport's screen region
2. Call `GridRenderer::Draw(viewport.ActiveGrid, viewport.BuildRenderData(), ctx)`
3. Call `WireframeRenderer::Draw(scene, viewport.BuildRenderData(), ctx)`
4. Call `SelectionRenderer::Draw(selection, scene, viewport.BuildRenderData(), ctx)`

### `GridRenderer`

- Each frame: CPU-generate line vertices from `GridPlane` for the current view extent
- Upload via existing scratch allocator from `RendererServices`
- Draw minor lines (grey, thin) then major lines (slightly brighter grey)
- Perspective viewport renders the XZ ground plane only
- Ortho viewports render the plane matching their axis

### `WireframeRenderer`

- Iterate `LevelScene::GetAllEntities()`, check for `CubePrimitive`
- Build 12-edge box line list from `HalfExtents` + world transform
- Draw in red (`vec4(1, 0, 0, 1)`) with depth test `LESS_OR_EQUAL` + small depth bias

### `SelectionRenderer`

- For each entity in `ISelectionContext::GetSelected()`: draw same box lines in white or yellow (distinguishable from red wireframe)

### Line Shader

Add a minimal `editor_line.vert.glsl` / `editor_line.frag.glsl` shader pair. Per-vertex: `vec3 Position`, `vec4 Color`. No textures, no lighting. Embed as SPIRV following the same `SenchaShaders.cmake` pattern as `mesh_forward`.

---

## System Registration

`EditorApp::OnRegisterSystems` registers:

```
EditorCameraSystem  →  FrameUpdate phase
```

`EditorCameraSystem::FrameUpdate`:
- Read active viewport's `EditorCamera`
- If perspective and right-mouse held: apply mouse delta to yaw/pitch, apply WASD movement
- Write updated camera state back to `EditorViewport`

No fixed-tick systems in the editor. The fixed-step loop runs with no registered `FixedLogic` systems — this is harmless.

---

## Serialization

`LevelDocument::Save` calls `SaveSceneJson(Registry_, filePath)`.
`LevelDocument::Load` calls `LoadSceneJson(Registry_, filePath)` then `MarkDirty(false)`.

`LevelSerialization.cpp` registers:
- `ComponentSerializer<CubePrimitive>` — JSON key `"cube_primitive"`, FourCC `CUBE`
- `TypeSchema<CubePrimitive>` with `HalfExtents` field

Registration happens once at editor startup before any load.

---

## Phased Implementation Plan

### Phase 1 — Build and Boot

Goals: Editor compiles and opens an empty SDL window with ImGui menu bar.

- Add `editor/` to root `CMakeLists.txt`
- `main.cpp` → `Application app; return app.Run<EditorApp>();`
- `EditorApp::OnConfigure` — set title "Sencha Editor"
- `EditorApp::OnStart` — install `EditorUiFeature` only (no `EditorRenderFeature` yet)
- `EditorUiFeature` — ImGui init, empty docking space, menu bar with "File > Exit"
- Engine additions: `GridPlane.h`, `Grid3d.h` (headers only, no .cpp needed)

Verification: window opens, ImGui renders, "Exit" closes cleanly.

### Phase 2 — 4-Way Viewport and Grid

Goals: Four viewport regions visible with black backgrounds and grey grids.

- `EditorCamera`, `EditorViewport`, `FourWayViewportLayout`
- `ViewportPanel : IEditorPanel` — four `BeginChild` regions
- `EditorRenderFeature` registered before `EditorUiFeature`
- `GridRenderer` — line geometry from `GridPlane`, grey lines
- Perspective viewport: fly-cam controls (right-click + WASD)
- Ortho viewports: fixed cameras for Top/Front/Side

Verification: four regions drawn, grid lines visible in all four, fly-cam navigates in top-left.

### Phase 3 — Tool System and Select Tool

Goals: Clicking in a viewport selects an entity (none yet, but the path works).

- `ITool`, `ToolRegistry`, `ToolContext`
- `ISelectionContext`, `SelectionContext`, `SelectionService`
- `ICommand`, `CommandStack`
- `PickingService` (CPU AABB stub — returns empty if no entities)
- `SelectTool` — left-click → `PickingService::Pick` → `SelectCommand`
- `SelectCommand` — updates `ISelectionContext`, undoable
- `ToolPalettePanel` — buttons call `ToolRegistry::Activate`

Verification: tool palette switches active tool, select tool click is a no-op (no entities yet), undo/redo does not crash.

### Phase 4 — Cube and Camera Tools

Goals: Place cubes and cameras in viewports; wireframes render in red.

- `CubePrimitive` struct + `TypeSchema` registration
- `LevelScene`, `LevelDocument`, `LevelWorkspace`
- `CreateCubeCommand`, `CreateCameraCommand`, `DeleteEntityCommand`
- `CubeTool` — click → `CreateCubeCommand` at world-space grid snap position
- `CameraTool` — click → `CreateCameraCommand`
- `WireframeRenderer` — red box wireframes for `CubePrimitive` entities
- `SelectionRenderer` — highlight selected entities in yellow
- `SelectTool` now returns actual hit results; selection highlight appears

Verification: place cubes in perspective and ortho views; select them; undo/redo cube creation.

### Phase 5 — UI Panels

Goals: Scene hierarchy and inspector show live data.

- `SceneHierarchyPanel` — lists entities by `EntityId`; click to select (via `SelectCommand`)
- `InspectorPanel` — shows `Transform3f` fields and `CubePrimitive::HalfExtents` for selected entity using `TypeSchema` introspection; edits go through `SetTransformCommand`
- `InspectorPanel` never writes fields directly — each field edit produces a command

Verification: selecting entity in hierarchy highlights it in viewport; editing position in inspector moves entity.

### Phase 6 — Level Serialization

Goals: Save and load a level file.

- `LevelSerialization.cpp` — register `ComponentSerializer<CubePrimitive>`
- `LevelDocument::Save` / `Load` wired to `SaveSceneJson` / `LoadSceneJson`
- "File > Save" / "File > Open" in menu bar — use SDL file dialog or hardcoded path for now
- Window title shows `*` when dirty

Verification: place cubes, save, reopen editor, load — cubes reappear in correct positions.

---

## Non-Goals

The following will not be implemented in this milestone. Do not design toward them:

- Brush/CSG geometry — `CubeTool` is explicitly temporary
- Material assignment or editing
- Prefab authoring or instance tracking
- Undo tree (branching history)
- GPU picking
- Multi-document editing (multiple open levels)
- Orthographic pan and zoom
- Transform gizmos (drag handles in viewport)
- Asset browser
- Bone/animation tooling
- Multi-window SDL setup
- Event-driven rendering (editor redraws every frame)
- Physics simulation preview

---

## Risks

### ImGui Vulkan + SDL3 Backend Ownership
`ImGuiDebugOverlay` already initializes ImGui backends. The editor must not initialize them a second time. In editor builds, `ImGuiDebugOverlay` must not be registered. Verify there is no shared init path that adds it automatically.

### `vkCmdSetScissor` Per-Viewport
Four viewport regions inside one render pass requires `vkCmdSetScissor` and `vkCmdSetViewport` to change between viewports. Confirm the Vulkan pipeline used by `GridRenderer` and `WireframeRenderer` is created with `VK_DYNAMIC_STATE_SCISSOR` and `VK_DYNAMIC_STATE_VIEWPORT`.

### `LevelDocument` vs. `ZoneRuntime`
The editor owns its own `Registry` inside `LevelDocument`, separate from the game's `ZoneRuntime`. This avoids contaminating the runtime scene with editor state. When play-in-editor is added later, a separate runtime zone will load from the saved file. Do not share the registry between editor and runtime.

### `CubePrimitive` Scope Creep
`CubePrimitive` must not be referenced by engine code, physics, or the runtime renderer. It lives in `editor/level/` only. If anything outside `editor/level/` starts depending on it, that is an architectural violation.

### Line Shader Pipeline
Adding `editor_line.vert.glsl` / `editor_line.frag.glsl` requires SPIRV embedding via `SenchaShaders.cmake`. Follow the exact same pattern used for `mesh_forward`. Confirm the shader embed step runs as part of the `sencha_editor` target, not `sencha_engine`.
