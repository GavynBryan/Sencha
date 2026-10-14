#pragma once

#include "BrushPreviewRenderer.h"
#include "BrushSolidRenderer.h"
#include "ComponentVisualRenderer.h"
#include "EditorFillPipeline.h"
#include <graphics/vulkan/ColoredVertexPipelines.h>
#include "EditorSolidPipeline.h"
#include "EditorBloomPass.h"
#include "render/EditorWideLinePipeline.h"
#include "viewport/EditorViewport.h"
#include "GpuGridRenderer.h"
#include "IrradianceVolumeRenderer.h"
#include "VolumeRenderer.h"
#include "render/ViewportTargetCache.h"
#include "IBrushBodyRenderer.h"
#include "BrushFillRenderer.h"
#include "SceneRenderQueueBuilder.h"
#include "SceneWireframeRenderer.h"
#include "EditorInstancedFillPipeline.h"
#include "PlacementGhost.h"
#include "SceneThumbnailCache.h"
#include "ThumbnailStudio.h"
#include "SceneSolidRenderer.h"
#include "SelectionRenderer.h"
#include "ShadowResidencyReadout.h"
#include "StaticMeshRenderer.h"
#include "ViewportBackdropRenderer.h"
#include "WireframeRenderer.h"
#include "ZoneBoundsRenderer.h"
#include "ZoneGraphRenderer.h"
#include "AffordanceRenderer.h"
#include "AnnotationStrokeRenderer.h"

#include "viewport/ViewportShading.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/SkyGradientPass.h>
#include <render/FrameComposition.h>
#include <render/ui/UiDrawFrame.h>
#include <render/pass/MeshForwardPass.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidency.h>
#include <render/pass/ShadowDepthPass.h>
#include <world/registry/RegistryId.h>

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
class PlacementSession;
class PreviewBuffer;
class SceneSourceStage;
class SelectionService;
class ViewportLayout;
class AssetSystem;
class AssetRegistry;
class LoggingProvider;
class ConsoleRegistry;
class EditorAffordanceService;
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
                        EditorAffordanceService& affordances,
                        SelectionService& selection,
                        MeshEditService& meshEdit,
                        const EditorOverlayState& overlay,
                        PreviewBuffer& preview,
                        const PlacementSession& placement,
                        std::function<const ManipulatorSession*()> session,
                        const GridSettings& grid,
                        WorldViewSettings& worldView,
                        LoggingProvider& logging,
                        const ConsoleRegistry& console,
                        AssetSystem* assets,
                        const AssetRegistry* catalog,
                        RuntimeAssets* runtimeAssets);

    // The look and budget cvars it reads each frame, archived, so a session
    // keeps what was dialled in. Registered before it first draws.
    static void RegisterCVars(ConsoleRegistry& console);

    // Offscreen: this feature renders each viewport into its own texture before the
    // swapchain (MainColor) pass opens; the UI then composites those textures.
    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

    // The viewport offscreen targets, sized by whatever arranges the views.
    // Owned here so its GPU resources tear down with this feature.
    [[nodiscard]] ViewportTargetCache& GetViewportTargets() { return Targets; }

    // The host image each shown viewport is drawn as, from whatever arranges
    // the views. Each rendered view publishes its image under its host image
    // once the image is readable; a view with none renders unseen.
    struct ViewImage
    {
        ViewportId Viewport;
        UiHostImageId Image = kInvalidUiHostImage;
    };
    void SetViewImages(std::vector<ViewImage> images) { ViewImages = std::move(images); }

    // The frame's shadow arbitration snapshot, read by the lighting panel.
    [[nodiscard]] const ShadowResidencyReadout& ShadowReadout() const
    {
        return ShadowFrame;
    }
    // Re-renders every cached shadow tile (panel button; edits that bypass
    // extracted state, like the depth-bias cvars, invalidate automatically).
    void InvalidateShadows() { Residency.InvalidateAll(); }

    // The focus document's queue builder, for the baked-lighting preview
    // controls (lighting panel). Null before Setup or without asset stores.
    // Scene thumbnails for the browser; null until the WYSIWYG material path
    // is up (no thumbnails without real materials to draw them with).
    [[nodiscard]] SceneThumbnailCache* SceneThumbnails()
    {
        return Thumbnails ? &*Thumbnails : nullptr;
    }

    [[nodiscard]] PlacementGhost* PlacementGhosts() { return Ghost ? &*Ghost : nullptr; }

    [[nodiscard]] SceneRenderQueueBuilder* FocusQueueBuilder()
    {
        return QueueBuilder.has_value() ? &*QueueBuilder : nullptr;
    }

private:
    // One live viewport and the target it renders into, resolved before the
    // frame is composed so a declared view can name the target and a record
    // body can find its way back to the panel.
    struct ViewSlot
    {
        EditorViewport* Viewport = nullptr;
        ViewportTargetCache::RenderView Target;
    };

    // The composition's entry point for a viewport view: recovers the slot the
    // view was declared with and renders it with the camera the view carries.
    // Drops targets for viewports not in LiveViewports.
    void PruneViewportTargets();
    // Hands each rendered view's image to the UI under its host image; takes
    // back any whose view did not render readable this frame.
    void PublishViewImages();
    void RecordViewportView(const FrameContext& frame, const FrameView& view);
    // Render one viewport's scene chain into its offscreen color+depth target, with
    // the surrounding layout transitions and rendering scope.
    void RenderViewportOffscreen(const FrameContext& frame, EditorViewport& viewport,
                                 const ViewportTargetCache::RenderView& target,
                                 const CameraRenderData& camera);
    // Runs the focus scene's shadow arbitration and records the scheduled
    // depth views, then publishes the panel snapshot. Called once per frame
    // before any viewport renders.
    void SweepBrushBakes();
    void DrawPlacementGhost(const FrameContext& frame, const EditorViewport& viewport,
                            const CameraRenderData& camera, const SceneSourceStage& ghost);
    void UpdateShadowResidency(const FrameContext& frame);
    // The camera position shadow scores rank against: the active viewport if
    // it is perspective, else the first perspective viewport, else the active
    // viewport of any orientation. Ortho positions still rank deterministically.
    [[nodiscard]] Vec<3> ShadowScoreOrigin() const;
    // Renders the active wireframe glow source and composites the bloom onto the scene
    // color (no-op when the viewport has no bloom target). Runs after the scene pass.
    void RecordViewportBloom(const FrameContext& frame, EditorViewport& viewport,
                             const ViewportTargetCache::RenderView& target,
                             const CameraRenderData& camera);

    WorldDocument&         World;
    std::function<const ManipulatorSession*()> Session;
    ViewportLayout& Layout;
    const GridSettings&    GridCfg;
    // Mutable: the streaming preview stores its sticky focus here per frame.
    WorldViewSettings& WorldView;
    GridStyle              GridStyleCache{}; // refreshed per frame from editor.grid.* cvars
    ViewportBackdropRenderer Backdrop;
    // Drawn instead of the backdrop in perspective viewports, so what the
    // editor shows matches what the game shows. Ortho viewports keep the
    // backdrop: a sky in a 2D working view describes nothing.
    SkyGradientPass        Sky;
    GpuGridRenderer        Grid;
    // Declared before the renderers that bind a reference to it at construction.
    // (The feature owns the one shared solid pipeline.)
    EditorSolidPipeline    Solid;
    BrushSolidRenderer     BrushSolid;
    // GPU residency for every open document's brush meshes, shared by the
    // focus and context builders so a brush open in two zones uploads once.
    // Declared before the builders: they hold a reference to it.
    BrushBakeCache         BrushBakes;
    // Solid preview of placed static meshes; shares the one Solid pipeline above.
    StaticMeshRenderer     Meshes;
    // WYSIWYG material path: drives the runtime forward pass with the scene's real
    // materials. Active whenever an asset environment is present (essentially always);
    // BrushSolid/Meshes above are the procedural-checker fallback, kept until the
    // owner's pixel-diff confirms the editor composite is gamma-correct (then removed).
    // Lighting owns the set-2 bindings plus the spot shadow atlas; ShadowPass
    // records the focus scene's scheduled tiles once per frame before the
    // viewport loop, so every Solid viewport samples the same atlas the game
    // would render. Context zones draw with shadow-free light sets.
    LightBindings          Lighting;
    ShadowDepthPass        ShadowPass;
    // The same arbiter the game pipeline runs, scored against the focus
    // viewport's camera; the caster diff feeds its OnChange invalidation.
    ShadowResidency        Residency;
    ShadowCasterDiff       CasterDiff;
    std::vector<ShadowCasterEvent> CasterEvents;
    ShadowResidencyReadout ShadowFrame;
    // Slot state describes one scene's lights; a focus or document switch
    // (new scene registry) resets the arbiter instead of letting stale
    // holders age out through steal hysteresis.
    RegistryId             ShadowSceneRegistry;
    // Depth-bias cvars bake into rendered tiles, so a change re-renders them.
    float                  ShadowBiasConstant = -1.0f;
    float                  ShadowBiasSlope = -1.0f;
    MeshForwardPass        Forward;
    std::optional<SceneRenderQueueBuilder> QueueBuilder;
    std::optional<ThumbnailStudio> Studio;
    std::optional<SceneThumbnailCache> Thumbnails;
    // The pending prefab placement, drawn where it stands.
    const PlacementSession& Placement;
    std::optional<PlacementGhost> Ghost;
    std::optional<SceneSolidRenderer>      SceneSolid;
    // Wireframe body over the builder's retained draws (instanced lines);
    // declared after the pipelines and the builder it references.
    std::optional<SceneWireframeRenderer>  SceneWire;
    // One WYSIWYG queue builder per open context zone (lazily created, dropped
    // when the zone closes): context zones render their real materials dimmed
    // by the draw-level tint instead of the procedural-checker fallback. Idle
    // zones cost their placements only (baked meshes live in the shared cache).
    std::unordered_map<uint64_t, std::unique_ptr<SceneRenderQueueBuilder>> ContextBuilders;
    RuntimeAssets*     RuntimeAssetsRef = nullptr;
    LoggingProvider*   LoggingRef = nullptr;
    StaticMeshCache*       MeshCache = nullptr;        // for the unconditional MeshQueue draw
    const SkinnedMeshCache* SkinnedMeshCacheRef = nullptr; // rest-pose skinned draws, WYSIWYG
    MaterialCache*         MaterialStore = nullptr;
    bool                   MaterialPath = false;
    // Declared before the line renderers: they bind a reference to it at
    // construction. (The feature owns the one shared line pipeline.)
    ColoredVertexPipeline     Lines;
    EditorInstancedLinePipeline InstancedLines;
    EditorInstancedFillPipeline InstancedFill;
    WireframeRenderer      Wireframe;
    ComponentVisualRenderer Visuals;
    // Selection feedback strokes draw through the wide-line pipeline (exact pixel
    // width + analytic AA); face fills through the blended triangle pipeline. Both
    // declared before Highlight, which binds them by reference.
    EditorWideLinePipeline WideLines;
    EditorFillPipeline     Fills;
    SelectionRenderer      Highlight;
    BrushFillRenderer      BrushFills;
    VolumeRenderer         Volumes;
    ZoneBoundsRenderer     ZoneBounds;
    ZoneGraphRenderer      ZoneGraph;
    IrradianceVolumeRenderer IrradianceVolumes;
    AffordanceRenderer     Affordances;
    AnnotationStrokeRenderer Annotations;
    // Create-drag preview overlay; runs in every viewport (not a body strategy).
    BrushPreviewRenderer   Preview;
    // Per-viewport offscreen targets this feature renders into; the UI composites them.
    ViewportTargetCache    Targets;
    // What this frame is made of. The shadow atlas is arbitrated and recorded
    // once as work; every viewport is a view that waits on it. The ordering
    // used to be a comment above the viewport loop.
    FrameComposition       Composition;
    DependencyPointId      ShadowAtlasReady;
    // Retained across frames: rebuilt every frame, but the storage is not, and
    // a declared view holds a pointer into ViewSlots until the frame executes.
    std::vector<ViewSlot>  ViewSlots;
    std::vector<ViewportId> LiveViewports;
    std::vector<ViewImage> ViewImages;
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
