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
#include <render/static_mesh/StaticMeshCache.h>

struct GraphicsServices;
class VulkanSwapchainService;

//=============================================================================
// DefaultRenderPipeline
//
// Collects the engine's built-in render state and extraction behavior.
// Bridges scene data, asset stores, and render features into a RenderPacket.
//=============================================================================
class DefaultRenderPipeline
{
public:
    explicit DefaultRenderPipeline(LoggingProvider* logging = nullptr);

    [[nodiscard]] RenderQueue& GetRenderQueue() { return Queue; }
    [[nodiscard]] const RenderQueue& GetRenderQueue() const { return Queue; }

    [[nodiscard]] CameraRenderData& GetCameraData() { return Camera; }
    [[nodiscard]] const CameraRenderData& GetCameraData() const { return Camera; }

    void SetAssetStores(StaticMeshCache& meshes, MaterialCache& materials, MaterialSetCache& materialSets);
    bool AddMeshRenderFeature(GraphicsServices& graphics);
    void ExtractRender(RenderExtractContext& ctx);

private:
    RenderQueue Queue;
    RenderLightSet Lights;
    CameraRenderData Camera;
    StaticMeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
    MaterialSetCache* MaterialSets = nullptr;
    Logger* Log = nullptr;
    bool LightCapWarned = false;
    // Cached at AddMeshRenderFeature (wiring time) for the surface extent used
    // during extraction; null until then, and in non-Vulkan builds.
    VulkanSwapchainService* Swapchain = nullptr;

    LightExtractionSystem LightExtractor;
    RenderExtractionSystem RenderExtractor;
};
