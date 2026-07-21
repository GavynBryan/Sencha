#pragma once

#include <app/DefaultRenderPipeline.h>
#include <app/EngineSchedule.h>
#include <core/console/ConsoleStartupScript.h>
#include <core/config/EngineConfig.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <profiling/CpuScopeTimings.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>
#include <runtime/FrameTrace.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/TimingHistory.h>
#include <zone/ZoneRuntime.h>

#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <profiling/RenderCapture.h>
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class AsyncTaskQueue;
class AudioService;
class CaptionRuntime;
class ConsoleService;
class DebugService;
class FrameDriver;
class Game;
class GpuTimestampPool;
struct GraphicsServices;
class IDebugPanel;
class ImGuiDebugOverlay;
class JobSystem;
struct PlatformServices;
class RuntimeWorld;
class ThreadPoolJobSystem;

//=============================================================================
// Engine
//
// Owns the runtime services, frame loop, unified entity world, schedule, and
// timing state. Initializes, runs, and shuts down the core engine around a Game
// instance.
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
    void SetStartupScript(ConsoleStartupScript script) { StartupScript = std::move(script); }
    [[nodiscard]] bool IsInitialized() const { return Initialized; }

    [[nodiscard]] EngineConfig& Config() { return Configuration; }
    [[nodiscard]] const EngineConfig& Config() const { return Configuration; }

    // Foundation: owned before any service or system so it outlives everything
    // that logs during teardown. Injected by reference into the things that
    // need it; never registered as a service.
    [[nodiscard]] LoggingProvider& Logging() { return LoggingState; }
    [[nodiscard]] const LoggingProvider& Logging() const { return LoggingState; }

    // Always-present singleton services. Valid between Initialize and Shutdown.
    [[nodiscard]] DebugService& Debug();
    [[nodiscard]] const DebugService& Debug() const;
    [[nodiscard]] AudioService& Audio();
    [[nodiscard]] const AudioService& Audio() const;
    [[nodiscard]] CaptionRuntime& Captions();
    [[nodiscard]] const CaptionRuntime& Captions() const;
    [[nodiscard]] ConsoleService& Console();
    [[nodiscard]] const ConsoleService& Console() const;

    // SDL platform services. Present only when the engine runs windowed
    // (GraphicsApi != None); valid between Initialize and Shutdown.
    [[nodiscard]] PlatformServices& Platform();
    [[nodiscard]] const PlatformServices& Platform() const;

    // Nullable form for code paths that must work headless: returns nullptr
    // when platform services are absent instead of asserting. Prefer Platform()
    // on the required path; use this only where headless is a valid state.
    [[nodiscard]] PlatformServices* TryPlatform();
    [[nodiscard]] const PlatformServices* TryPlatform() const;

#ifdef SENCHA_ENABLE_VULKAN
    // Vulkan backend services. Present when running windowed; valid between
    // Initialize and Shutdown.
    [[nodiscard]] GraphicsServices& Graphics();
    [[nodiscard]] const GraphicsServices& Graphics() const;

    // Nullable form; returns nullptr when graphics services are absent
    // (headless / no Vulkan device) instead of asserting.
    [[nodiscard]] GraphicsServices* TryGraphics();
    [[nodiscard]] const GraphicsServices* TryGraphics() const;
#endif

    [[nodiscard]] EngineSchedule& Schedule() { return EngineSystems; }
    [[nodiscard]] const EngineSchedule& Schedule() const { return EngineSystems; }

    // Complete engine-plus-game runtime component vocabulary for this run.
    // Valid after Game::OnRegisterRuntimeComponents and before the game module
    // is detached. The unified runtime world applies it exactly once before its
    // first entity is created.
    [[nodiscard]] WorldComponentSchema& RuntimeComponents()
    {
        return RuntimeComponentSchemaState;
    }
    [[nodiscard]] const WorldComponentSchema& RuntimeComponents() const
    {
        return RuntimeComponentSchemaState;
    }

    // One runtime entity universe for the current simulation. Constructed after
    // the schema is sealed and valid through game/system shutdown.
    [[nodiscard]] RuntimeWorld& World();
    [[nodiscard]] const RuntimeWorld& World() const;

    // Legacy runtime zone registry owner. This remains only while PR #130 ports
    // its callers and is deleted before the cutover can merge.
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

    // The renderer instrumentation bundle. Always present; its members are
    // non-null exactly while their render.profile.mode tier is active (all
    // null when profiling is compiled out).
    [[nodiscard]] const RenderInstrumentation& Instrumentation() const
    {
        return InstrumentationBundle;
    }
    [[nodiscard]] RenderProfileMode ActiveRenderProfileMode() const
    {
        return ActiveProfileMode;
    }
    // Applies the pending render.profile.mode once, at the top of the
    // extract phase, so one frame never sees two modes; resets the frame's
    // stats while counters are active.
    void ApplyPendingRenderProfileMode();
    // Pushes the finished frame's stats into the history ring; called after
    // the render phase so pass publishes are included. No-op below Counters.
    void PushRenderStatsFrame();
    // Stamps the capture envelope with the device, driver, and build a run
    // was recorded on. Called once graphics exist, before any frame runs.
    void PublishCaptureEnvironment();

    [[nodiscard]] DefaultRenderPipeline* GetRenderPipeline();
    [[nodiscard]] const DefaultRenderPipeline* GetRenderPipeline() const;

#ifdef SENCHA_ENABLE_DEBUG_UI
    // The runtime debug overlay (console + timing panels, grave-key toggle).
    // Created by Run when windowed and Config().Console.UiEnabled. Null when
    // headless, opted out, or before Run.  Owned by the renderer as a render
    // feature.
    [[nodiscard]] ImGuiDebugOverlay* GetDebugOverlay() { return DebugOverlayFeature; }
    // Adds a game panel to the overlay. Valid any time after Initialize: the
    // overlay itself is created after the game's startup hooks (so it draws
    // over every scene feature), and panels added before then are queued and
    // attached at creation. Dropped when the overlay is opted out or absent.
    void AddDebugPanel(std::unique_ptr<IDebugPanel> panel);
#endif

private:
    void RegisterFramePhases(Game& game);
    void RegisterEngineConsoleBuiltins(ConsoleService& console, DebugService& debug);
    // Adds the default debug overlay to the main renderer. Called by Run after
    // the game's startup hooks so the overlay's MainColor draw follows every
    // scene feature the game registered.
    void CreateDebugOverlay();

    EngineConfig Configuration;
    // Foundation: declared before every service and system so that -- members
    // destroy in reverse declaration order -- logging outlives everything whose
    // destructor may log.
    LoggingProvider LoggingState;
    // Always-present services, valid between Initialize and Shutdown. Declared
    // after LoggingState (they may log on teardown) and before the SDL/Vulkan
    // groups (they do not depend on those, so they tear down after them).
    std::unique_ptr<DebugService> DebugState;
    // Console after Debug: its built-in cvars reference DebugState, so it must
    // tear down (reverse declaration order) before the service it points at.
    std::unique_ptr<ConsoleService> ConsoleState;
    std::unique_ptr<AudioService> AudioState;
    std::unique_ptr<CaptionRuntime> CaptionState;
    // SDL platform group. Declared before the Vulkan group so the window
    // outlives the surface/swapchain that depend on it. Null when headless.
    std::unique_ptr<PlatformServices> PlatformState;
#ifdef SENCHA_ENABLE_VULKAN
    // Vulkan group. Declared after PlatformState so it tears down first
    // (surface/swapchain reference the window). Null when headless.
    std::unique_ptr<GraphicsServices> GraphicsState;
#endif
    EngineSchedule EngineSystems;
    WorldComponentSchema RuntimeComponentSchemaState;
    std::unique_ptr<RuntimeWorld> RuntimeWorldState;
    ZoneRuntime ZoneRuntimeState;
    RuntimeFrameLoop RuntimeLoop;
    ConsoleStartupScript StartupScript;
    std::unique_ptr<FrameDriver> FrameDriverInstance;
    TimingHistory TimingData;
    // Run-control state, written by app.exit_after_frames / frame.trace.output.
    // Zero and empty leave both facilities inert; the trace store is allocated
    // only for a run that armed an output path.
    std::uint64_t ExitAfterFrames = 0;
    std::string FrameTraceOutputPath;
    std::unique_ptr<ChromeJsonFrameTrace> FrameTraceStore;
    // Written by render.capture.output; the render capture is flushed here at
    // shutdown when profiling is compiled in and the run recorded in capture mode.
    std::string RenderCaptureOutputPath;
    // Instrumentation storage beside the timing history. The bundle's
    // pointers select into these per the active mode; the objects exist in
    // every build so consumers stay #ifdef-free at the publish points.
    RenderInstrumentation InstrumentationBundle;
    RenderStats FrameRenderStats;
    RenderStatsHistory RenderStatsRing;
    CpuScopeTimings FrameCpuScopes;
    RenderProfileMode ActiveProfileMode = RenderProfileMode::Off;
    // Written by the render.profile.mode cvar; consumed by the frame latch.
    RenderProfileMode PendingProfileMode = RenderProfileMode::Off;
    std::uint64_t RenderStatsFrameIndex = 0;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    RenderCapture RenderCaptureStore;
#endif
#if defined(SENCHA_ENABLE_RENDER_PROFILING) && defined(SENCHA_ENABLE_VULKAN)
    // Behind a pointer so the module ABI surface stays free of Vulkan types.
    // Created with graphics; its query pools are created lazily on the first
    // Gpu-mode frame and destroyed in Shutdown while the device is alive.
    std::unique_ptr<GpuTimestampPool> GpuTimestampsPool;
#endif
#ifdef SENCHA_ENABLE_DEBUG_UI
    // Borrowed view of the renderer-owned overlay feature (event forwarding
    // and panel registration); cleared when graphics tear down.
    ImGuiDebugOverlay* DebugOverlayFeature = nullptr;
    // Panels added before the overlay exists, attached at creation.
    std::vector<std::unique_ptr<IDebugPanel>> PendingDebugPanels;
#endif
    // Declared last: destroyed first, so task/worker threads are joined (and
    // pending commits dropped) before the worlds and services they reference.
    std::unique_ptr<AsyncTaskQueue> TaskQueueInstance;
    std::unique_ptr<ThreadPoolJobSystem> FramePoolInstance;
    bool Initialized = false;
    bool Running = false;
    bool FramePhasesRegistered = false;
};
