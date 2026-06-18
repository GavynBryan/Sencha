#pragma once

#include <app/DefaultRenderPipeline.h>
#include <app/EngineSchedule.h>
#include <core/config/EngineConfig.h>
#include <core/logging/LoggingProvider.h>
#include <core/service/ServiceHost.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/TimingHistory.h>
#include <zone/ZoneRuntime.h>

#include <memory>

class AsyncTaskQueue;
class FrameDriver;
class Game;
class JobSystem;
class ThreadPoolJobSystem;

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

    // Foundation: owned before any service or system so it outlives everything
    // that logs during teardown. Injected by reference into the things that
    // need it; never registered as a service.
    [[nodiscard]] LoggingProvider& Logging() { return LoggingState; }
    [[nodiscard]] const LoggingProvider& Logging() const { return LoggingState; }

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

    // Async (cross-frame) task lane. Valid after Initialize. Commits run on
    // the main thread each frame in FramePhase::DrainAsyncTasks.
    [[nodiscard]] AsyncTaskQueue& Tasks();
    [[nodiscard]] const AsyncTaskQueue& Tasks() const;

    // Frame-lane fork-join pool. Valid after Initialize. Sized by
    // EngineRuntimeConfig::JobWorkerCount (0 = single-threaded, the
    // engine-wide bisect/determinism switch).
    [[nodiscard]] JobSystem& Jobs();
    [[nodiscard]] const JobSystem& Jobs() const;

    [[nodiscard]] TimingHistory& Timing() { return TimingData; }
    [[nodiscard]] const TimingHistory& Timing() const { return TimingData; }

    [[nodiscard]] DefaultRenderPipeline* GetRenderPipeline();
    [[nodiscard]] const DefaultRenderPipeline* GetRenderPipeline() const;

private:
    void RegisterFramePhases(Game& game);

    EngineConfig Configuration;
    // Declared before ServiceRegistry/EngineSystems: members destroy in reverse
    // declaration order, so logging outlives the services and systems whose
    // destructors may log.
    LoggingProvider LoggingState;
    ServiceHost ServiceRegistry;
    EngineSchedule EngineSystems;
    ZoneRuntime ZoneRuntimeState;
    RuntimeFrameLoop RuntimeLoop;
    std::unique_ptr<FrameDriver> FrameDriverInstance;
    TimingHistory TimingData;
    // Declared last: destroyed first, so task/worker threads are joined (and
    // pending commits dropped) before the zones and services they reference.
    std::unique_ptr<AsyncTaskQueue> TaskQueueInstance;
    std::unique_ptr<ThreadPoolJobSystem> FramePoolInstance;
    bool Initialized = false;
    bool Running = false;
    bool FramePhasesRegistered = false;
};
