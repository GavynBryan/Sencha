#pragma once

#include <app/GameContexts.h>
#include <core/config/EngineConfig.h>
#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <render/Camera.h>
#include <render/DefaultRenderScene.h>
#include <render/Material.h>
#include <render/MeshRendererComponent.h>
#include <render/MeshService.h>
#include <render/RenderQueue.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/TimingHistory.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPresentationStore.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>
#include <zone/ZoneRuntime.h>

#include <memory>

class FrameDriver;
class Game;

class Engine
{
public:
    explicit Engine(EngineConfig config);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    bool Initialize();
    void Shutdown();
    int Run(Game& game);
    void RequestExit() { Running_ = false; }
    [[nodiscard]] bool IsInitialized() const { return Initialized_; }

    [[nodiscard]] EngineConfig& Config() { return Config_; }
    [[nodiscard]] const EngineConfig& Config() const { return Config_; }

    [[nodiscard]] ServiceHost& Services() { return Services_; }
    [[nodiscard]] const ServiceHost& Services() const { return Services_; }

    [[nodiscard]] SystemHost& Systems() { return Systems_; }
    [[nodiscard]] const SystemHost& Systems() const { return Systems_; }

    [[nodiscard]] ZoneRuntime& Zones() { return Zones_; }
    [[nodiscard]] const ZoneRuntime& Zones() const { return Zones_; }

    [[nodiscard]] RuntimeFrameLoop& Runtime() { return Runtime_; }
    [[nodiscard]] const RuntimeFrameLoop& Runtime() const { return Runtime_; }

    [[nodiscard]] FrameDriver* Driver() { return Driver_.get(); }
    [[nodiscard]] const FrameDriver* Driver() const { return Driver_.get(); }

    [[nodiscard]] TimingHistory& Timing() { return Timing_; }
    [[nodiscard]] const TimingHistory& Timing() const { return Timing_; }

    [[nodiscard]] RenderQueue& GetRenderQueue() { return RenderQueue_; }
    [[nodiscard]] const RenderQueue& GetRenderQueue() const { return RenderQueue_; }

    [[nodiscard]] CameraRenderData& GetCameraData() { return CameraData_; }
    [[nodiscard]] const CameraRenderData& GetCameraData() const { return CameraData_; }

    void RegisterDefaultRenderScene(DefaultRenderScene scene);
    bool AddDefaultMeshRenderFeature(MeshService& meshes, MaterialStore& materials);

private:
    void RegisterFramePhases(Game& game);
    bool ExtractDefaultRenderScene(RenderExtractContext& ctx);

    EngineConfig Config_;
    ServiceHost Services_;
    SystemHost Systems_;
    ZoneRuntime Zones_;
    RuntimeFrameLoop Runtime_;
    std::unique_ptr<FrameDriver> Driver_;
    TimingHistory Timing_;
    RenderQueue RenderQueue_;
    CameraRenderData CameraData_;
    DefaultRenderScene DefaultRenderScene_;
    bool Initialized_ = false;
    bool Running_ = false;
    bool FramePhasesRegistered_ = false;
};
