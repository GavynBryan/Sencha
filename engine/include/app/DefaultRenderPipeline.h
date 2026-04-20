#pragma once

#include <app/GameContexts.h>
#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/MeshCache.h>
#include <render/RenderQueue.h>

class ServiceHost;

//=============================================================================
// DefaultRenderPipeline
//
// Collects the engine's built-in render state and extraction behavior.
// Bridges scene data, asset stores, and render features into a RenderPacket.
//=============================================================================
class DefaultRenderPipeline
{
public:
    [[nodiscard]] RenderQueue& GetRenderQueue() { return Queue; }
    [[nodiscard]] const RenderQueue& GetRenderQueue() const { return Queue; }

    [[nodiscard]] CameraRenderData& GetCameraData() { return Camera; }
    [[nodiscard]] const CameraRenderData& GetCameraData() const { return Camera; }

    void SetAssetStores(MeshCache& meshes, MaterialCache& materials);
    bool AddMeshRenderFeature(ServiceHost& services);
    void ExtractRender(RenderExtractContext& ctx);

private:
    RenderQueue Queue;
    CameraRenderData Camera;
    MeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
};
