#pragma once

#include <render/MaterialCache.h>   // MaterialHandle
#include <render/LightSelection.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidencyTypes.h>
#include <render/static_mesh/StaticMeshHandle.h>

class SkinnedMeshCache;

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

class EditorDocument;
class AssetSystem;
class StaticMeshCache;
class MaterialSetCache;
class TextureCache;
class LoggingProvider;
class Logger;
struct Registry;

//=============================================================================
// SceneRenderQueueBuilder
//
// Produces the editor's per-frame draw queues from the document so the Solid
// viewport renders the exact GpuStaticMesh + bindless Material the runtime
// ships (WYSIWYG). Brushes are tessellated and baked through the SAME brush
// cook kernel the offline cook and PIE use (CollectCookBrushes +
// BakeBrushFacesToStaticMesh), then uploaded to the shared StaticMeshCache;
// placed meshes are already GPU-resident (loaded through the AssetSystem), so
// their handles are emitted directly.
//
// Two queues because the per-viewport draw policy differs: brushes follow the
// viewport's shading mode (only Solid viewports draw them through here),
// placed meshes draw in every viewport. Both are camera-independent and built
// once per frame; the per-viewport camera is applied at draw time.
//
// The light set carries the scene's packed lights; shadowed point and spot
// lights are gathered as candidates and emitted as residency requests on
// demand, so the editor runs the same arbiter the game does.
// The caster set gathers the brush and placed-mesh sections that cast
// (engine caster policy) plus the per-entity diff records driving OnChange
// invalidation, so the shadow depth pass renders the same atlas the game
// would for this scene.
//
// CPU/asset only (no Vulkan) so it can be unit-tested headlessly.
//=============================================================================
class SceneRenderQueueBuilder
{
public:
    // `skinnedMeshes` is optional the same way `textures` is: without it,
    // skinned placements simply emit nothing. Non-const because the preview
    // registry's component lifecycle retains through it.
    SceneRenderQueueBuilder(AssetSystem& assets,
                            StaticMeshCache& meshes,
                            MaterialCache& materials,
                            MaterialSetCache& materialSets,
                            LoggingProvider& logging,
                            TextureCache* textures = nullptr,
                            SkinnedMeshCache* skinnedMeshes = nullptr);
    ~SceneRenderQueueBuilder();

    SceneRenderQueueBuilder(const SceneRenderQueueBuilder&) = delete;
    SceneRenderQueueBuilder& operator=(const SceneRenderQueueBuilder&) = delete;

    // Rebuild both queues from the given document (per-call so the workspace can
    // swap the edited document without touching this builder). Brush geometry is
    // re-baked and re-uploaded only when the scene's brushes changed since the
    // last call (whole-scene content hash, so an idle frame uploads nothing);
    // placed-mesh items are re-emitted each call (their GPU meshes are owned by
    // the asset system, not here).
    void Build(const EditorDocument& document);

    // Scores the gathered shadow candidates against the given origin (the
    // focus viewport's camera) and emits residency requests, score descending
    // with stable key ties: the order the arbiter treats as priority. Unlike
    // the game's extraction there is no frustum cull; every viewport samples
    // the one atlas, so the camera only ranks, never excludes.
    [[nodiscard]] std::span<const SpotShadowRequest> BuildShadowRequests(
        const Vec<3>& viewOrigin);
    [[nodiscard]] std::span<const PointShadowRequest> BuildPointShadowRequests(
        const Vec<3>& viewOrigin);

    // Baked-lighting preview: while enabled and loaded, the solid queues show
    // the LAST COOK's scene (cells with atlas UVs, placements with their
    // cooked scale/bias, Direct lights excluded) instead of the live brush
    // solids. The preview is a snapshot: it goes stale when the document
    // diverges from the cook that produced it (the badge; another cook
    // refreshes it). Selection, wireframe, and gizmo overlays are untouched.
    struct LightmapPreviewSource
    {
        std::filesystem::path CookedScenePath;
        std::uint64_t CookHash = 0;
    };
    void SetLightmapPreview(const LightmapPreviewSource& source);
    void SetLightmapPreviewEnabled(bool enabled) { PreviewEnabled = enabled; }
    [[nodiscard]] bool LightmapPreviewEnabled() const { return PreviewEnabled; }
    [[nodiscard]] bool LightmapPreviewLoaded() const { return PreviewRegistry != nullptr; }
    [[nodiscard]] bool LightmapPreviewStale() const { return PreviewStale; }

    [[nodiscard]] const RenderQueue& BrushQueue() const { return Brushes; }
    [[nodiscard]] const RenderQueue& MeshQueue() const { return PlacedMeshes; }
    [[nodiscard]] const RenderLightSet& Lights() const { return SceneLights; }
    [[nodiscard]] RenderLightSet& Lights() { return SceneLights; }
    [[nodiscard]] const ShadowCasterSet& Casters() const { return SceneCasters; }
    [[nodiscard]] ShadowCasterSet& Casters() { return SceneCasters; }

private:
    // One cooked brush's GPU mesh, owned here (Create/Destroy), plus the material
    // handle per material slot (index = StaticMeshSection::MaterialSlot).
    struct CachedBrushMesh
    {
        StaticMeshHandle Mesh;
        std::vector<MaterialHandle> SlotMaterials;
    };

    void RebuildBrushMeshes(const EditorDocument& document);
    void EmitBrushQueue();
    void EmitPreviewQueue();
    void BuildMeshQueue(const EditorDocument& document);
    void BuildLights(const EditorDocument& document, bool skipDirectLights);
    void BuildShadowCasters(const EditorDocument& document);
    void ReleaseBrushMeshes();

    AssetSystem& Assets;
    StaticMeshCache& Meshes;
    MaterialCache& Materials;
    MaterialSetCache& MaterialSets;
    TextureCache* Textures = nullptr;
    SkinnedMeshCache* SkinnedMeshes = nullptr;
    LoggingProvider& Logging;
    Logger& Log;

    std::vector<CachedBrushMesh> BrushMeshes;     // GPU brush meshes, one per cooked brush
    std::vector<MaterialHandle> BrushMaterials;   // material refs this build holds (released on rebuild)
    uint64_t BrushHash = 0;                       // content hash of the last bake
    bool HasBaked = false;

    // The cooked-scene snapshot backing the baked-lighting preview, loaded
    // through the editor's asset caches. DocHash captures the document state
    // (brush, light, and probe-volume content) the snapshot corresponds to;
    // divergence flags the stale badge but keeps rendering the snapshot (no
    // flicker).
    std::unique_ptr<Registry> PreviewRegistry;
    std::uint64_t PreviewDocHash = 0;
    std::uint64_t CurrentDocHash = 0;
    std::uint64_t LightsHash = 0;                 // folded during BuildLights
    bool PreviewEnabled = false;
    bool PreviewStale = false;

    RenderQueue Brushes;
    RenderQueue PlacedMeshes;
    RenderLightSet SceneLights;
    ShadowCasterSet SceneCasters;
    std::vector<ForwardLightCandidate> LightCandidates;
    bool LightSelectionCurrent = false;
    std::vector<SpotShadowRequest> ShadowRequests;
    std::vector<PointShadowRequest> PointShadowRequests;
};
