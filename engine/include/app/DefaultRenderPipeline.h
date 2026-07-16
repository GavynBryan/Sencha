#pragma once

#include <app/GameContexts.h>
#include <core/logging/LoggingProvider.h>
#include <render/Camera.h>
#include <render/LightExtractionSystem.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
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
                        MaterialSetCache& materialSets);
    bool AddMeshRenderFeature(GraphicsServices& graphics);
    void ExtractRender(RenderExtractContext& ctx);

    // Marks every cached shadow slot for re-render (the
    // render.shadow.invalidate console command).
    void InvalidateShadows() { Residency.InvalidateAll(); }

private:
    RenderQueue Queue;
    RenderLightSet Lights;
    ShadowCasterSet ShadowCasters;
    ShadowResidency Residency;
    ShadowCasterDiff CasterDiff;
    std::vector<ShadowCasterEvent> CasterEvents;
    std::vector<SpotShadowRequest> ShadowRequests;
    CameraRenderData Camera;
    StaticMeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
    MaterialSetCache* MaterialSets = nullptr;
    Logger* Log = nullptr;
    const ConsoleRegistry* Console = nullptr;
    bool LightCapWarned = false;
    VulkanSwapchainService* Swapchain = nullptr;

    LightExtractionSystem LightExtractor;
    ShadowCasterExtractionSystem ShadowCasterExtractor;
    RenderExtractionSystem RenderExtractor;
};
