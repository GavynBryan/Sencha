#pragma once

#include <app/DefaultRenderPipeline.h>
#include <app/EngineSchedule.h>
#include <core/config/EngineConfig.h>
#include <core/service/ServiceHost.h>
#include <render/Material.h>
#include <render/MeshService.h>
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

    [[nodiscard]] EngineSchedule& Schedule() { return Schedule_; }
    [[nodiscard]] const EngineSchedule& Schedule() const { return Schedule_; }

    [[nodiscard]] ZoneRuntime& Zones() { return Zones_; }
    [[nodiscard]] const ZoneRuntime& Zones() const { return Zones_; }

    [[nodiscard]] RuntimeFrameLoop& Runtime() { return Runtime_; }
    [[nodiscard]] const RuntimeFrameLoop& Runtime() const { return Runtime_; }

    [[nodiscard]] FrameDriver* Driver() { return Driver_.get(); }
    [[nodiscard]] const FrameDriver* Driver() const { return Driver_.get(); }

    [[nodiscard]] TimingHistory& Timing() { return Timing_; }
    [[nodiscard]] const TimingHistory& Timing() const { return Timing_; }

    [[nodiscard]] RenderQueue& GetRenderQueue();
    [[nodiscard]] const RenderQueue& GetRenderQueue() const;

    [[nodiscard]] CameraRenderData& GetCameraData();
    [[nodiscard]] const CameraRenderData& GetCameraData() const;

    bool AddDefaultMeshRenderFeature(MeshService& meshes, MaterialStore& materials);

private:
    void RegisterFramePhases(Game& game);
    [[nodiscard]] DefaultRenderPipeline& GetDefaultRenderPipeline();
    [[nodiscard]] const DefaultRenderPipeline& GetDefaultRenderPipeline() const;

    EngineConfig Config_;
    ServiceHost Services_;
    EngineSchedule Schedule_;
    ZoneRuntime Zones_;
    RuntimeFrameLoop Runtime_;
    std::unique_ptr<FrameDriver> Driver_;
    TimingHistory Timing_;
    bool Initialized_ = false;
    bool Running_ = false;
    bool FramePhasesRegistered_ = false;
};
