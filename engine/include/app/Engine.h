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
#include <world/serialization/ComponentSerializerRegistry.h>

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
class SdlGamepadCapture;
class JobSystem;
struct PlatformServices;
class RuntimeWorld;

// Owns the runtime services, frame loop, unified entity world, schedule, and
// timing state. There is exactly one runtime entity universe per Engine run.
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
    void SetStartupScript(ConsoleStartupScript script)
    {
        StartupScript = std::move(script);
    }
    [[nodiscard]] bool IsInitialized() const { return Initialized; }

    [[nodiscard]] EngineConfig& Config() { return Configuration; }
    [[nodiscard]] const EngineConfig& Config() const
    {
        return Configuration;
    }

    [[nodiscard]] LoggingProvider& Logging() { return LoggingState; }
    [[nodiscard]] const LoggingProvider& Logging() const
    {
        return LoggingState;
    }

    [[nodiscard]] DebugService& Debug();
    [[nodiscard]] const DebugService& Debug() const;
    [[nodiscard]] AudioService& Audio();
    [[nodiscard]] const AudioService& Audio() const;
    [[nodiscard]] CaptionRuntime& Captions();
    [[nodiscard]] const CaptionRuntime& Captions() const;
    [[nodiscard]] ConsoleService& Console();
    [[nodiscard]] const ConsoleService& Console() const;

    [[nodiscard]] PlatformServices& Platform();
    [[nodiscard]] const PlatformServices& Platform() const;
    [[nodiscard]] PlatformServices* TryPlatform();
    [[nodiscard]] const PlatformServices* TryPlatform() const;

#ifdef SENCHA_ENABLE_VULKAN
    [[nodiscard]] GraphicsServices& Graphics();
    [[nodiscard]] const GraphicsServices& Graphics() const;
    [[nodiscard]] GraphicsServices* TryGraphics();
    [[nodiscard]] const GraphicsServices* TryGraphics() const;
#endif

    [[nodiscard]] EngineSchedule& Schedule() { return EngineSystems; }
    [[nodiscard]] const EngineSchedule& Schedule() const
    {
        return EngineSystems;
    }

    // Complete engine-plus-game runtime component vocabulary for this run.
    [[nodiscard]] WorldComponentSchema& RuntimeComponents()
    {
        return RuntimeComponentSchemaState;
    }
    [[nodiscard]] const WorldComponentSchema& RuntimeComponents() const
    {
        return RuntimeComponentSchemaState;
    }

    // The sole runtime entity universe for this simulation. Persistent entities
    // live in partition zero and streamed zones occupy storage partitions inside
    // the same World.
    [[nodiscard]] RuntimeWorld& World();
    [[nodiscard]] const RuntimeWorld& World() const;

    [[nodiscard]] RuntimeFrameLoop& Runtime() { return RuntimeLoop; }
    [[nodiscard]] const RuntimeFrameLoop& Runtime() const
    {
        return RuntimeLoop;
    }

    [[nodiscard]] AsyncTaskQueue& Tasks();
    [[nodiscard]] const AsyncTaskQueue& Tasks() const;

    // The frame-lane fork/join pool. Its consumers are editor and asset-side —
    // source watching, project content mount, texture recook — and the runtime
    // frame deliberately has none: the zone-axis parallelism it was sized for was
    // measured not to pay at room scale and was retired with per-registry storage.
    // Disjoint storage partitions remain a valid axis if a workload ever clears the
    // dispatch floor. See docs/ecs/parallelization.md.
    [[nodiscard]] JobSystem& Jobs();
    [[nodiscard]] const JobSystem& Jobs() const;

    // Which components this host can serialize: the engine scene manifest plus
    // whatever the loaded game module registered through OnRegisterComponents.
    // Zone loading and any other scene read/write takes this explicitly.
    [[nodiscard]] const ComponentSerializerRegistry& SceneSerializers() const
    {
        return SceneSerializerRegistry;
    }

    [[nodiscard]] DefaultRenderPipeline* GetRenderPipeline();
    [[nodiscard]] const DefaultRenderPipeline* GetRenderPipeline() const;

    // Open gamepads, or null when the platform layer has not been brought up.
    // Present regardless of the debug UI: pads are ordinary input hardware.
    [[nodiscard]] SdlGamepadCapture* GetGamepadCapture() { return GamepadCaptureState.get(); }

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
    // Frame-phase bodies and the state they sample. Registered once, from
    // EngineFramePhases.cpp; nothing outside the frame pipeline reads these.
    void RegisterFramePhases(Game& game);
    [[nodiscard]] TimingHistory& Timing() { return TimingData; }
    // The renderer instrumentation bundle. Always present; its members are
    // non-null exactly while their render.profile.mode tier is active (all
    // null when profiling is compiled out).
    [[nodiscard]] const RenderInstrumentation& Instrumentation() const
    {
        return InstrumentationBundle;
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

    void RegisterEngineConsoleBuiltins(ConsoleService& console, DebugService& debug);
    // Adds the default debug overlay to the main renderer. Called by Run after
    // the game's startup hooks so the overlay's MainColor draw follows every
    // scene feature the game registered.
    void CreateDebugOverlay();

    EngineConfig Configuration;
    ComponentSerializerRegistry SceneSerializerRegistry;
    LoggingProvider LoggingState;
    std::unique_ptr<DebugService> DebugState;
    std::unique_ptr<ConsoleService> ConsoleState;
    std::unique_ptr<AudioService> AudioState;
    std::unique_ptr<CaptionRuntime> CaptionState;
    std::unique_ptr<PlatformServices> PlatformState;
    // Owns the open gamepads. Stateful, unlike the keyboard and mouse adapter:
    // a pad has to be held open to report anything.
    std::unique_ptr<SdlGamepadCapture> GamepadCaptureState;
#ifdef SENCHA_ENABLE_VULKAN
    std::unique_ptr<GraphicsServices> GraphicsState;
#endif
    EngineSchedule EngineSystems;
    WorldComponentSchema RuntimeComponentSchemaState;
    std::unique_ptr<RuntimeWorld> RuntimeWorldState;
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
    std::unique_ptr<JobSystem> FramePoolInstance;
    bool Initialized = false;
    bool Running = false;
    bool FramePhasesRegistered = false;
};
