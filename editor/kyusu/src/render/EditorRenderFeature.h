#pragma once

#include "BrushPreviewRenderer.h"
#include "BrushSolidRenderer.h"
#include "ComponentVisualRenderer.h"
#include "EditorFillPipeline.h"
#include "EditorLinePipeline.h"
#include "EditorSolidPipeline.h"
#include "EditorBloomPass.h"
#include "EditorWideLinePipeline.h"
#include "GpuGridRenderer.h"
#include "render/ViewportTargetCache.h"
#include "IBrushBodyRenderer.h"
#include "BrushFillRenderer.h"
#include "SceneRenderQueueBuilder.h"
#include "SceneSolidRenderer.h"
#include "SelectionRenderer.h"
#include "StaticMeshRenderer.h"
#include "ViewportBackdropRenderer.h"
#include "WireframeRenderer.h"
#include "ZoneBoundsRenderer.h"

#include "viewport/ViewportShading.h"

#include <graphics/vulkan/Renderer.h>
#include <render/MeshForwardPass.h>
#include <render/SpotShadowDepthPass.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class EditorScene;
class EditorDocument;
class WorldDocument;
class ManipulatorSession;
class MeshEditService;
class PreviewBuffer;
class SelectionService;
class ViewportLayout;
class AssetSystem;
class AssetRegistry;
class LoggingProvider;
class ConsoleRegistry;
struct WorldViewSettings;
class StaticMeshCache;
class MaterialCache;
struct GridSettings;
struct EditorOverlayState;
struct RuntimeAssets;

class EditorRenderFeature : public IRenderFeature
{
public:
    // The focus document and the manipulator session are rebuilt when the
    // workspace resets interaction state, so both are resolved per frame: the
    // document through the world document, the session through an injected
    // resolver. Stable workspace value members (layout, selection, mesh edit,
    // overlay, preview, grid) are bound as plain references.
    EditorRenderFeature(ViewportLayout& viewportLayout,
                        WorldDocument& world,
                        SelectionService& selection,
                        MeshEditService& meshEdit,
                        const EditorOverlayState& overlay,
                        PreviewBuffer& preview,
                        std::function<const ManipulatorSession*()> session,
                        const GridSettings& grid,
                        WorldViewSettings& worldView,
                        LoggingProvider& logging,
                        const ConsoleRegistry& console,
                        AssetSystem* assets,
                        const AssetRegistry* catalog,
                        RuntimeAssets* runtimeAssets);

    // Offscreen: this feature renders each viewport into its own texture before the
    // swapchain (MainColor) pass opens; the UI then composites those textures.
    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

    // The viewport offscreen targets, shared with ViewportPanel (which displays them
    // via ImGui::Image). Owned here so its GPU resources tear down with this feature.
    [[nodiscard]] ViewportTargetCache& GetViewportTargets() { return Targets; }

    // Release the scene queues' GPU brush meshes + material refs. The feature itself
    // tears down later in ~Renderer (after the engine frees graphics), but these handles
    // borrow the asset caches, so EditorServices calls this before it resets the asset
    // system, mirroring how the document's StaticMeshComponents release first.
    void ReleaseSceneResources();

private:
    // Render one viewport's scene chain into its offscreen color+depth target, with
    // the surrounding layout transitions and rendering scope.
    void RenderViewportOffscreen(const FrameContext& frame, EditorViewport& viewport,
                                 const ViewportTargetCache::RenderView& target);
    // Renders the active wireframe glow source and composites the bloom onto the scene
    // color (no-op when the viewport has no bloom target). Runs after the scene pass.
    void RecordViewportBloom(const FrameContext& frame, EditorViewport& viewport,
                             const ViewportTargetCache::RenderView& target);

    WorldDocument&         World;
    std::function<const ManipulatorSession*()> Session;
    ViewportLayout& Layout;
    const GridSettings&    GridCfg;
    // Mutable: the streaming preview stores its sticky focus here per frame.
    WorldViewSettings& WorldView;
    GridStyle              GridStyleCache{}; // refreshed per frame from editor.grid.* cvars
    ViewportBackdropRenderer Backdrop;
    GpuGridRenderer        Grid;
    // Declared before the renderers that bind a reference to it at construction.
    // (The feature owns the one shared solid pipeline.)
    EditorSolidPipeline    Solid;
    BrushSolidRenderer     BrushSolid;
    // Solid preview of placed static meshes; shares the one Solid pipeline above.
    StaticMeshRenderer     Meshes;
    // WYSIWYG material path: drives the runtime forward pass with the scene's real
    // materials. Active whenever an asset environment is present (essentially always);
    // BrushSolid/Meshes above are the procedural-checker fallback, kept until the
    // owner's pixel-diff confirms the editor composite is gamma-correct (then removed).
    // Lighting owns the set-2 bindings plus the spot shadow atlas; ShadowPass
    // records the focus scene's granted tiles once per frame before the
    // viewport loop, so every Solid viewport samples the same atlas the game
    // would render. Context zones draw with shadow-free light sets.
    LightBindings          Lighting;
    SpotShadowDepthPass    ShadowPass;
    // Rebuilt per frame from the scene's fixed grants (one 512 tile per
    // granted slot, re-rendered every frame).
    std::vector<SpotShadowViewJob> ShadowJobs;
    MeshForwardPass        Forward;
    std::optional<SceneRenderQueueBuilder> QueueBuilder;
    std::optional<SceneSolidRenderer>      SceneSolid;
    // One WYSIWYG queue builder per open context zone (lazily created, dropped
    // when the zone closes): context zones render their real materials dimmed
    // by the draw-level tint instead of the procedural-checker fallback. Idle
    // zones cost nothing (the builder's content hash skips re-bakes).
    std::unordered_map<uint64_t, std::unique_ptr<SceneRenderQueueBuilder>> ContextBuilders;
    RuntimeAssets*     RuntimeAssetsRef = nullptr;
    LoggingProvider*   LoggingRef = nullptr;
    StaticMeshCache*       MeshCache = nullptr;        // for the unconditional MeshQueue draw
    MaterialCache*         MaterialStore = nullptr;
    bool                   MaterialPath = false;
    // Declared before the line renderers: they bind a reference to it at
    // construction. (The feature owns the one shared line pipeline.)
    EditorLinePipeline     Lines;
    WireframeRenderer      Wireframe;
    ComponentVisualRenderer Visuals;
    // Selection feedback strokes draw through the wide-line pipeline (exact pixel
    // width + analytic AA); face fills through the blended triangle pipeline. Both
    // declared before Highlight, which binds them by reference.
    EditorWideLinePipeline WideLines;
    EditorFillPipeline     Fills;
    SelectionRenderer      Highlight;
    BrushFillRenderer      BrushFills;
    ZoneBoundsRenderer     ZoneBounds;
    // Create-drag preview overlay; runs in every viewport (not a body strategy).
    BrushPreviewRenderer   Preview;
    // Per-viewport offscreen targets this feature renders into; the UI composites them.
    ViewportTargetCache    Targets;
    EditorBloomPass        Bloom;
    bool                   BloomEnabled = true;     // editor.bloom.enable
    BloomParams            BloomParamsCache{};       // editor.bloom.threshold/intensity/radius
    RendererServices       Services{};
    // Brush-body strategy per ViewportShading; the draw loop indexes this by the
    // viewport's shading. A new shading mode registers its strategy here — the
    // draw loop never changes.
    std::array<IBrushBodyRenderer*, ViewportShadingCount> BodyRenderers{};
    const ConsoleRegistry* Console        = nullptr;
    Logger*                Log            = nullptr;
    bool                   LoggedFirstDraw = false;
};
