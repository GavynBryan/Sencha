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
#include "SceneRenderQueueBuilder.h"
#include "SceneSolidRenderer.h"
#include "SelectionRenderer.h"
#include "StaticMeshRenderer.h"
#include "ViewportBackdropRenderer.h"
#include "WireframeRenderer.h"

#include "viewport/ViewportShading.h"

#include <graphics/vulkan/Renderer.h>
#include <render/MeshForwardPass.h>

#include <array>
#include <optional>

class EditorScene;
class EditorDocument;
class ManipulatorSession;
class MeshEditService;
class PreviewBuffer;
class SelectionService;
class ViewportLayout;
class AssetSystem;
class AssetRegistry;
class LoggingProvider;
class ConsoleRegistry;
class StaticMeshCache;
class MaterialCache;
struct GridSettings;
struct EditorOverlayState;
struct RuntimeAssets;

class EditorRenderFeature : public IRenderFeature
{
public:
    EditorRenderFeature(ViewportLayout& viewportLayout,
                        EditorScene& scene,
                        SelectionService& selection,
                        MeshEditService& meshEdit,
                        const EditorOverlayState& overlay,
                        PreviewBuffer& preview,
                        ManipulatorSession& session,
                        const GridSettings& grid,
                        LoggingProvider& logging,
                        const ConsoleRegistry& console,
                        AssetSystem* assets,
                        const AssetRegistry* catalog,
                        RuntimeAssets* runtimeAssets,
                        const EditorDocument& document);

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

    ViewportLayout& Layout;
    const GridSettings&    GridCfg;
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
    MeshForwardPass        Forward;
    std::optional<SceneRenderQueueBuilder> QueueBuilder;
    std::optional<SceneSolidRenderer>      SceneSolid;
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
