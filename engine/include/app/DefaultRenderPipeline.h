#pragma once

#include <app/GameContexts.h>
#include <core/logging/LoggingProvider.h>
#include <profiling/RenderInstrumentation.h>
#include <render/extract/Camera.h>
#include <render/extract/LightExtractionSystem.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/ProbeVolumeSet.h>
#include <render/extract/RenderExtractionSystem.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/extract/ShadowCasterExtractionSystem.h>
#include <render/SkinnedPoseFrameData.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidency.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <vector>

struct GraphicsServices;
class ConsoleRegistry;
class VulkanSwapchainService;

//=============================================================================
// DefaultRenderPipeline
//
// Collects the engine's built-in render state and extraction behavior. Bridges
// scene data, asset stores, renderer configuration, and render features into
// the render queue and light set the registered features draw from.
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

    // `clips` and `skeletons` arrive together: a clip poses a skeleton, and
    // without both, skinned instances render at bind pose.
    void SetAssetStores(StaticMeshCache& meshes, MaterialCache& materials,
                        MaterialSetCache& materialSets,
                        TextureCache* textures = nullptr,
                        const SkinnedMeshCache* skinnedMeshes = nullptr,
                        const AnimationClipCache* clips = nullptr,
                        const SkeletonCache* skeletons = nullptr);
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

    // Empties every per-frame collection the features read. Runs before any
    // path that can abandon extraction, so a frame that cannot extract
    // publishes an empty frame rather than last frame's: the features run
    // unconditionally and would otherwise re-record a stale shadow schedule
    // and re-dispatch stale skinning against a camera that no longer exists.
    void PublishEmptyFrameOutputs();

    RenderQueue Queue;
    RenderLightSet Lights;
    // The frame's skinned instances and their palettes. Extraction fills it,
    // the Offscreen pose feature poses them, the mesh feature draws them; the
    // shared pointer is the channel between the two features.
    std::shared_ptr<SkinnedPoseFrameData> SkinnedPoses;
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
    const SkinnedMeshCache* SkinnedMeshes = nullptr;
    const AnimationClipCache* AnimationClips = nullptr;
    const SkeletonCache* Skeletons = nullptr;
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
