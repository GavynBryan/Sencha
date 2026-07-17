#pragma once

#include <render/MaterialCache.h>   // MaterialHandle
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidency.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>
#include <span>
#include <vector>

class EditorDocument;
class AssetSystem;
class StaticMeshCache;
class MaterialSetCache;
class LoggingProvider;
class Logger;

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
    SceneRenderQueueBuilder(AssetSystem& assets,
                            StaticMeshCache& meshes,
                            MaterialCache& materials,
                            MaterialSetCache& materialSets,
                            LoggingProvider& logging);
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

    // One shadow-casting spot light gathered by BuildLights, scored and
    // emitted by BuildShadowRequests once the frame's view origin is known.
    struct SpotShadowCandidate
    {
        RenderEntityKey Key;
        std::uint32_t LightIndex = UINT32_MAX;
        Vec<3> Position;
        float Range = 0.0f;
        float Intensity = 0.0f;
        SpotShadowView View;
        Sphere Bounds;
        std::uint32_t TileSize = 0;
        ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
    };

    struct PointShadowCandidate
    {
        RenderEntityKey Key;
        std::uint32_t LightIndex = UINT32_MAX;
        Vec<3> Position;
        float Range = 0.0f;
        float Intensity = 0.0f;
        PointShadowView View;
        Sphere Bounds;
        ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
    };

    void RebuildBrushMeshes(const EditorDocument& document);
    void EmitBrushQueue();
    void BuildMeshQueue(const EditorDocument& document);
    void BuildLights(const EditorDocument& document);
    void BuildShadowCasters(const EditorDocument& document);
    void ReleaseBrushMeshes();

    AssetSystem& Assets;
    StaticMeshCache& Meshes;
    MaterialCache& Materials;
    MaterialSetCache& MaterialSets;
    Logger& Log;

    std::vector<CachedBrushMesh> BrushMeshes;     // GPU brush meshes, one per cooked brush
    std::vector<MaterialHandle> BrushMaterials;   // material refs this build holds (released on rebuild)
    uint64_t BrushHash = 0;                       // content hash of the last bake
    bool HasBaked = false;

    RenderQueue Brushes;
    RenderQueue PlacedMeshes;
    RenderLightSet SceneLights;
    ShadowCasterSet SceneCasters;
    std::vector<SpotShadowCandidate> ShadowCandidates;
    std::vector<PointShadowCandidate> PointShadowCandidates;
    std::vector<SpotShadowRequest> ShadowRequests;
    std::vector<PointShadowRequest> PointShadowRequests;
};
