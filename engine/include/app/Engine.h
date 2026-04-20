#pragma once

#include <app/DefaultRenderPipeline.h>
#include <app/EngineSchedule.h>
#include <core/config/EngineConfig.h>
#include <core/service/ServiceHost.h>
#include <render/Material.h>
#include <render/MaterialCache.h>
#include <render/MeshCache.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/TimingHistory.h>
#include <zone/ZoneRuntime.h>

#include <memory>

class FrameDriver;
class Game;

//=============================================================================
// Engine
//
// Owns the runtime services, frame loop, world zones, schedule, and timing state.
// Initializes, runs, and shuts down the core engine around a Game instance.
//=============================================================================
class Engine
{
public:
    explicit Engine(EngineConfig engineConfig);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    bool Initialize();
    void Shutdown();
    int Run(Game& game);
    void RequestExit() { Running = false; }
    [[nodiscard]] bool IsInitialized() const { return Initialized; }

    [[nodiscard]] EngineConfig& Config() { return Configuration; }
    [[nodiscard]] const EngineConfig& Config() const { return Configuration; }

    [[nodiscard]] ServiceHost& Services() { return ServiceRegistry; }
    [[nodiscard]] const ServiceHost& Services() const { return ServiceRegistry; }

    [[nodiscard]] EngineSchedule& Schedule() { return EngineSystems; }
    [[nodiscard]] const EngineSchedule& Schedule() const { return EngineSystems; }

    [[nodiscard]] ZoneRuntime& Zones() { return ZoneRuntimeState; }
    [[nodiscard]] const ZoneRuntime& Zones() const { return ZoneRuntimeState; }

    [[nodiscard]] RuntimeFrameLoop& Runtime() { return RuntimeLoop; }
    [[nodiscard]] const RuntimeFrameLoop& Runtime() const { return RuntimeLoop; }

    [[nodiscard]] FrameDriver* Driver() { return FrameDriverInstance.get(); }
    [[nodiscard]] const FrameDriver* Driver() const { return FrameDriverInstance.get(); }

    [[nodiscard]] TimingHistory& Timing() { return TimingData; }
    [[nodiscard]] const TimingHistory& Timing() const { return TimingData; }

    [[nodiscard]] RenderQueue& GetRenderQueue();
    [[nodiscard]] const RenderQueue& GetRenderQueue() const;

    [[nodiscard]] CameraRenderData& GetCameraData();
    [[nodiscard]] const CameraRenderData& GetCameraData() const;

    bool AddDefaultMeshRenderFeature(MeshCache& meshes, MaterialCache& materials);

private:
    void RegisterFramePhases(Game& game);
    [[nodiscard]] DefaultRenderPipeline& GetDefaultRenderPipeline();
    [[nodiscard]] const DefaultRenderPipeline& GetDefaultRenderPipeline() const;

    EngineConfig Configuration;
    ServiceHost ServiceRegistry;
    EngineSchedule EngineSystems;
    ZoneRuntime ZoneRuntimeState;
    RuntimeFrameLoop RuntimeLoop;
    std::unique_ptr<FrameDriver> FrameDriverInstance;
    TimingHistory TimingData;
    bool Initialized = false;
    bool Running = false;
    bool FramePhasesRegistered = false;
};
