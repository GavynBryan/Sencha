#pragma once

#include <app/DefaultRenderPipeline.h>
#include <app/SessionParticipantProjection.h>
#include <net/NetMessageRouter.h>
#include <net/NetSession.h>
#include <app/EngineSchedule.h>
#include <core/console/ConsoleLineFeed.h>
#include <core/console/ConsoleStartupScript.h>
#include <core/config/EngineConfig.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <net/ReplicationLayout.h>
#include <net/NetCVarSync.h>
#include <net/NetSpawnPrefab.h>
#include <net/ClientPrediction.h>
#include <net/ReplicationInterpolation.h>
#include <net/NetStats.h>
#include <net/NetZoneStreaming.h>
#include <net/NetTickEstimator.h>
#include <net/PeerCommandRuntime.h>
#include <net/ReplicationRuntime.h>
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
#include <span>
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
class NetPrefabSpawner;
class SceneSpawnService;

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

    // The session, or null when this process is not hosting or joined. Null is
    // the normal case: a single-player game never constructs one, and every net
    // frame phase is a no-op without it. Callers check rather than assume, which
    // is why this is a Try and not a reference.
    [[nodiscard]] NetSession* TryNet() { return NetState.get(); }
    [[nodiscard]] const NetSession* TryNet() const { return NetState.get(); }
    // Constructs the session over `transport`, which the caller owns and must
    // outlive it. Returns null if one already exists.
    [[nodiscard]] NetSession* CreateNetSession(INetTransport& transport);
    void DestroyNetSession();

    // Per-session replication state: the authority's identity mint and per-peer
    // baselines, or a client's map of what it has been told about. Reset with
    // the session, because all of it is session-transient.
    [[nodiscard]] ReplicationRuntime& Replication() { return ReplicationState; }
    // Which session-owned cvar values each peer has been told. Reset with the
    // session like everything else that is session-transient.
    [[nodiscard]] NetCVarPublisher& CVarPublisher() { return CVarPublisherState; }
    // The input channel's per-peer arrival buffers, and the client half that
    // fills the wire. Session-transient for the same reason.
    [[nodiscard]] PeerCommandRuntime& PeerCommands() { return PeerCommandState; }
    [[nodiscard]] const PeerCommandRuntime& PeerCommands() const
    {
        return PeerCommandState;
    }
    // What the session is spending, as rates. Counting only: nothing reads it
    // to decide anything, so recording into it raises no ordering question.
    //-------------------------------------------------------------------------
    // World streaming
    //
    // A game that streams a world hands its partition runtime and loader over
    // once, and the engine drives both from then on: it updates streaming in
    // the zone-residency phase, keeps the world loaded around whoever this
    // machine drives, and -- in a session -- around every connected player as
    // well, offering each peer only its own neighbourhood.
    //
    // One call rather than four in a fixed order. Getting that order wrong was
    // silent, and a game had no way to know the order or reason to.
    //
    // Null on unload. The pointers are the game's to own; the engine only
    // borrows them, and holds nothing past a null.
    //-------------------------------------------------------------------------
    void SetWorldStreaming(WorldPartitionRuntime* partition,
                           AsyncZoneLoader* loader);
    [[nodiscard]] WorldPartitionRuntime* WorldStreaming() const
    {
        return StreamedWorld;
    }
    [[nodiscard]] AsyncZoneLoader* WorldStreamingLoader() const
    {
        return StreamedWorldLoader;
    }
    [[nodiscard]] NetZoneStreaming& ZoneStreaming() { return ZoneStreamingState; }

    [[nodiscard]] NetStats& NetTraffic() { return NetStatsState; }
    [[nodiscard]] const NetStats& NetTraffic() const { return NetStatsState; }
    // What the authority's clock is called, as seen from a client. Meaningless
    // on an authority, which is the machine defining it.
    [[nodiscard]] NetTickEstimator& NetClock() { return NetClockState; }
    [[nodiscard]] const NetTickEstimator& NetClock() const { return NetClockState; }
    // The one entity this machine simulates for itself rather than mirrors.
    // Inert until a client is given a pawn, which is every other configuration.
    [[nodiscard]] ClientPrediction& Prediction() { return PredictionState; }
    [[nodiscard]] const ClientPrediction& Prediction() const
    {
        return PredictionState;
    }
    // Everything else a client holds: mirrored along the authority's path at a
    // small delay rather than moved whenever a datagram lands. Inert on an
    // authority, which has nothing to mirror.
    [[nodiscard]] ReplicationInterpolation& Interpolation()
    {
        return InterpolationState;
    }
    [[nodiscard]] const ReplicationInterpolation& Interpolation() const
    {
        return InterpolationState;
    }

    // How a replicated entity that names a prefab is built on this machine.
    // Outlives any one session, because what it holds resident is content
    // rather than anything about a connection.
    [[nodiscard]] NetPrefabSpawner& NetPrefabs();

    // What a participant is made of, and where its body comes from. Registered
    // by the game, because it describes this game's idea of a player rather
    // than anything about a connection, so it outlives every session the
    // process runs.
    [[nodiscard]] ParticipantPolicies& Participants()
    {
        return ParticipantProjection.Policies();
    }
    [[nodiscard]] const ParticipantPolicies& Participants() const
    {
        return ParticipantProjection.Policies();
    }

    // Admits this process's own player through the same lifecycle a peer goes
    // through, and returns it. Idempotent: asking twice is the same person.
    //
    // Called by the game when its content is ready, because the engine cannot
    // observe a map load. Everything else about it is the engine's: a process
    // with no local player and a client both answer with nothing, the latter
    // because the authority owns every participant in a session and this
    // machine's arrives replicated like the rest.
    SessionParticipantAdmission AdmitLocalParticipant();

    // Adds a peerless simulated participant, such as a bot. Unlike the local
    // participant it does not take presentation control.
    SessionParticipantAdmission AdmitSimulatedParticipant(
        InputActionSourceId source);

    ParticipantBodyChange RequestParticipantBody(EntityId participant);
    ParticipantControlChange SetParticipantControlSubject(
        EntityId participant, EntityId subject);

    // Gives up this process's own player, if it has one. What it was driving is
    // let go of and its body reaped through the ordinary policy.
    //
    // Joining a session is the caller: a player provided locally before the
    // join is a second body standing where the authority's copy of this person
    // is about to appear.
    SessionParticipantRetirement RetireParticipant(EntityId participant);
    SessionParticipantRetirement RetireLocalParticipant();

    // Where a game's own payload kinds are answered. Registered by the game and
    // outlives any one session, because it describes what the game says rather
    // than who it is connected to -- the same lifetime as the spawn recipes.
    [[nodiscard]] NetMessageRouter& NetMessages() { return NetMessageState; }
    [[nodiscard]] const NetMessageRouter& NetMessages() const
    {
        return NetMessageState;
    }

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

    // Which components replicate and how their bytes are packed, compiled once
    // for this run. Sealed before OnStart, and read only by a session.
    [[nodiscard]] const ReplicationLayout& ReplicatedComponents() const
    {
        return ReplicationLayoutState;
    }

    // The sole runtime entity universe for this simulation. Persistent entities
    // live in partition zero and streamed zones occupy storage partitions inside
    // the same World.
    [[nodiscard]] RuntimeWorld& World();
    [[nodiscard]] const RuntimeWorld& World() const;

    // Runtime scene spawning: cooked scenes placed at play time, published in
    // request order at the async drain. Valid over the same span as World().
    // The game connects its asset stack once in OnStart
    // (Spawns().ConnectAssets(&runtimeAssets.Assets, &runtimeAssets.Scenes))
    // and disconnects it in OnShutdown before that stack goes away; requests
    // outside that span fail with a status, not a crash.
    [[nodiscard]] SceneSpawnService& Spawns();

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
    //
    // Split by what the bodies reach for, not by convenience: the simulation
    // half touches only the world, the schedule, and the clocks, so it runs
    // whether or not this process has a window. A headless host registers it
    // alone and steps the same frame the windowed host does, minus the phases
    // that would have had nothing to draw into.
    void RegisterFramePhases(Game& game);
    void RegisterHostCommandPhase();
    static void LogConsoleResult(Logger& log, const ConsoleResult& result);
    void RegisterSimulationFramePhases();
    void RegisterNetFramePhases();
    void RegisterPresentationFramePhases(Game& game);
    [[nodiscard]] bool HasPresentation() const;
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

    // Removes the serializers the game module registered. Must run while the
    // module is still mapped: the serializer objects were constructed by it,
    // and freeing one afterwards runs a destructor that is no longer there.
    void RetractGameComponents();

    // Everything session-transient, in one place, so creating and destroying a
    // session cannot reset two different lists of it.
    void ResetNetSessionState();

    EngineConfig Configuration;
    ComponentSerializerRegistry SceneSerializerRegistry;
    // Component identities the game module's registration added serializers
    // for, recorded so teardown does not need the game to list them again.
    std::vector<ComponentTypeId> GameSerializerTypes;
    LoggingProvider LoggingState;
    std::unique_ptr<DebugService> DebugState;
    std::unique_ptr<ConsoleService> ConsoleState;
    // Reads administration commands from the descriptor the process host named
    // (EngineConsoleConfig::CommandFd). Null unless it named one.
    std::unique_ptr<ConsoleLineFeed> CommandFeed;
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
    ReplicationLayout ReplicationLayoutState;
    ReplicationRuntime ReplicationState;
    NetMessageRouter NetMessageState;
    NetCVarPublisher CVarPublisherState;
    PeerCommandRuntime PeerCommandState;
    NetStats NetStatsState;
    NetZoneStreaming ZoneStreamingState;
    WorldPartitionRuntime* StreamedWorld = nullptr;
    AsyncZoneLoader* StreamedWorldLoader = nullptr;
    NetTickEstimator NetClockState;
    ClientPrediction PredictionState;
    ReplicationInterpolation InterpolationState;
    SessionParticipantProjection ParticipantProjection;
    std::unique_ptr<RuntimeWorld> RuntimeWorldState;
    // Constructed with the world; torn down before it (they borrow the world).
    std::unique_ptr<SceneSpawnService> SpawnServiceState;
    std::unique_ptr<NetPrefabSpawner> NetPrefabState;
    RuntimeFrameLoop RuntimeLoop;
    ConsoleStartupScript StartupScript;
    std::unique_ptr<FrameDriver> FrameDriverInstance;
    std::unique_ptr<NetSession> NetState;
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
