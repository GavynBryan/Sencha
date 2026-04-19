#pragma once

#include <app/GameContexts.h>
#include <core/service/IService.h>
#include <render/Camera.h>
#include <render/DefaultRenderScene.h>
#include <render/Material.h>
#include <render/MeshService.h>
#include <render/RenderQueue.h>

class ServiceHost;

class DefaultSceneBinding : public IService
{
public:
    void Reset();

    [[nodiscard]] RenderQueue& GetRenderQueue() { return Queue; }
    [[nodiscard]] const RenderQueue& GetRenderQueue() const { return Queue; }

    [[nodiscard]] CameraRenderData& GetCameraData() { return Camera; }
    [[nodiscard]] const CameraRenderData& GetCameraData() const { return Camera; }

    void RegisterScene(DefaultRenderScene scene);
    bool AddMeshRenderFeature(ServiceHost& services, MeshService& meshes, MaterialStore& materials);
    bool Extract(ServiceHost& services, RenderExtractContext& ctx);

private:
    RenderQueue Queue;
    CameraRenderData Camera;
    DefaultRenderScene Scene;
};
