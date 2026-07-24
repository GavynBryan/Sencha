#pragma once

#include <app/GameContexts.h>
#include <core/logging/LoggingProvider.h>
#include <profiling/RenderInstrumentation.h>
#include <render/Camera.h>
#include <render/LightExtractionSystem.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/ProbeVolumeSet.h>
#include <render/RenderExtractionSystem.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/ShadowCasterExtractionSystem.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidency.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <vector>

struct GraphicsServices;
class ConsoleRegistry;
class VulkanSwapchainService;

//=============================================================================
// DefaultRenderPipeline
//
// Collects the engine's built-in render state and extraction behavior. Bridges
// scene data, asset stores, renderer configuration, and render features into a
// RenderPacket.
//=============================================================================
class DefaultRenderPipeline
{
public:
    explicit DefaultRenderPipeline(LoggingProvider* logging = nullptr,
                                   const ConsoleRegistry* console = nullptr);

    [[nodiscard]] RenderQueue& GetRenderQueue() { return Queue; }
    [[nodiscard]] const RenderQueue& GetRenderQueue() const { return Queue; }

    [[nodiscard]] CameraRenderData& GetCameraData() { return Camera; }
    [[nodiscard]] const CameraRenderData& GetCameraData() const { return Camera; }

    void SetAssetStores(StaticMeshCache& meshes, MaterialCache& materials,
                        MaterialSetCache& materialSets,
                        TextureCache* textures = nullptr);
    bool AddMeshRenderFeature(GraphicsServices& graphics);
    void ExtractRender(RenderExtractContext& ctx);

    // The engine's instrumentation bundle; extraction publishes its frame
    // counters through it while the mode is Counters or above.
    void SetInstrumentation(const RenderInstrumentation* instrumentation)
    {
        Instrumentation = instrumentation;
    }

    // Marks every cached shadow slot for re-render (the
    // render.shadow.invalidate console command).
    void InvalidateShadows() { Residency.InvalidateAll(); }

    // Baked irradiance-probe residency. Hosts feed decoded .sprobe payloads
    // in at zone finalize; extraction appends the resident headers each frame.
    [[nodiscard]] ProbeVolumeSet& GetProbeVolumes() { return ProbeVolumes; }

private:
    void PublishExtractionStats(RenderStats& stats,
                                const LightExtractionCounts& lightCounts) const;

    RenderQueue Queue;
    RenderLightSet Lights;
    ProbeVolumeSet ProbeVolumes;
    ShadowCasterSet ShadowCasters;
    ShadowResidency Residency;
    ShadowCasterDiff CasterDiff;
    std::vector<ShadowCasterEvent> CasterEvents;
    // Whether last frame built the per-entity record table. A frame that did
    // not leaves the diff's retained table older than the gap, so the next one
    // that does must reseed rather than emit.
    bool CasterRecordsWereBuilt = false;
    std::vector<SpotShadowRequest> ShadowRequests;
    std::vector<PointShadowRequest> PointShadowRequests;
    CameraRenderData Camera;
    StaticMeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
    MaterialSetCache* MaterialSets = nullptr;
    TextureCache* Textures = nullptr;
    Logger* Log = nullptr;
    LoggingProvider* Logging = nullptr;
    const ConsoleRegistry* Console = nullptr;
    const RenderInstrumentation* Instrumentation = nullptr;
    bool LightCapWarned = false;
    VulkanSwapchainService* Swapchain = nullptr;

    LightExtractionSystem LightExtractor;
    ShadowCasterExtractionSystem ShadowCasterExtractor;
    RenderExtractionSystem RenderExtractor;
};
