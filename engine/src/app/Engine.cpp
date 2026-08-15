#include <app/Engine.h>
#include <app/SessionParticipantDiagnostics.h>
#include <app/EngineConsoleBuiltins.h>
#include <net/NetConsoleCommands.h>
#include <app/Game.h>
#include <audio/AudioService.h>
#include <audio/AudioSystem.h>
#include <audio/CaptionRuntime.h>
#include <audio/CaptionSystem.h>
#include <core/console/ConsoleService.h>
#include <core/logging/ConsoleLogSink.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <jobs/AsyncTaskQueue.h>
#include <jobs/JobSystem.h>
#include <prediction/PawnStateReplay.h>
#include <runtime/FrameDriver.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/GraphicsServices.h>
#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <graphics/vulkan/GpuTimestampPool.h>
#include <graphics/vulkan/VulkanDebugLabels.h>
#endif
#endif

#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/ConsolePanel.h>
#include <debug/ImGuiDebugOverlay.h>
#include <debug/NetStatsPanel.h>
#include <debug/TimingPanel.h>
#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <debug/RenderStatsPanel.h>
#endif
#endif

#include <input/SdlGamepadCapture.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <utility>

Engine::Engine(EngineConfig engineConfig)
    : Configuration(std::move(engineConfig))
{
}

Engine::~Engine()
{
    Shutdown();
}

bool Engine::Initialize()
{
    if (Initialized)
        return true;

    LoggingProvider& logging = LoggingState;
    if (Configuration.Debug.ConsoleLogging)
        logging.AddSink<ConsoleLogSink>();

    DebugLogSink& debugLog = logging.AddSink<DebugLogSink>();
    DebugState = std::make_unique<DebugService>(logging, debugLog);
    ConsoleState = std::make_unique<ConsoleService>();
    RegisterEngineConsoleBuiltins(*ConsoleState, *DebugState);
    RegisterNetConsoleCommands(ConsoleState->Registry(), *this);
    if (Configuration.Console.OpenOnStart)
        DebugState->Open();
    EngineSystems.Register<DefaultRenderPipeline>(
        &LoggingState, &ConsoleState->Registry());
    EngineConsoleBuiltins::RegisterRenderCommands(
        ConsoleState->Registry(), *EngineSystems.Get<DefaultRenderPipeline>());
    EngineSystems.Get<DefaultRenderPipeline>()->SetInstrumentation(
        &InstrumentationBundle);

    // A process with nothing to present has nobody listening to it either, so
    // it holds no playback device. Mixing still exists as a service; it simply
    // has no output, the same shape as a machine whose device failed to open.
    if (Configuration.Window.GraphicsApi == WindowGraphicsApi::None)
        Configuration.Audio.EnablePlayback = false;

    AudioState = std::make_unique<AudioService>(logging, Configuration.Audio);
    EngineSystems.Register<AudioSystem>(AudioState.get());

    CaptionState = std::make_unique<CaptionRuntime>(logging, Configuration.Captions);
    EngineSystems.Register<CaptionSystem>(CaptionState.get(), AudioState.get());
    auto failInitialize = [this]() {
        EngineSystems.Shutdown();
        NetState.reset();
        FrameDriverInstance.reset();
        TaskQueueInstance.reset();
        FramePoolInstance.reset();
        RuntimeWorldState.reset();
#ifdef SENCHA_ENABLE_VULKAN
        GraphicsState.reset();
#endif
        GamepadCaptureState.reset();
        PlatformState.reset();
        CaptionState.reset();
        AudioState.reset();
        ConsoleState.reset();
        DebugState.reset();
        LoggingState.Clear();
        FramePhasesRegistered = false;
        Running = false;
        return false;
    };

    RuntimeLoop.SetResizeSettleSeconds(Configuration.Runtime.ResizeSettleSeconds);
    RuntimeLoop.GetSimulationClock().SetFixedTickRate(Configuration.Runtime.FixedTickRate);
    RuntimeLoop.SetMaxFixedTicksPerFrame(
        static_cast<uint32_t>(Configuration.Runtime.MaxFixedTicksPerFrame));
    RuntimeLoop.SetMaxFrameWallDeltaSeconds(Configuration.Runtime.MaxFrameWallDeltaSeconds);

    TaskQueueInstance = std::make_unique<AsyncTaskQueue>(
        static_cast<uint32_t>(Configuration.Runtime.AsyncTaskThreadCount));

    const int configuredWorkers = Configuration.Runtime.JobWorkerCount;
    FramePoolInstance = std::make_unique<JobSystem>(
        configuredWorkers < 0 ? JobSystem::DefaultWorkerCount()
                              : static_cast<uint32_t>(configuredWorkers));

    // Headless: no platform, no graphics, but a real frame loop. The driver is
    // renderer-agnostic, so a host with nothing to draw into still steps ticks,
    // drains async commits, and runs its schedule. Nothing here blocks on a
    // display, so whether this spins a core is entirely down to the frame
    // target the host configured; the app sets one for a dedicated host, and a
    // target of zero (the engine default, and what the tests want) runs the
    // loop as fast as it can.
    if (Configuration.Window.GraphicsApi == WindowGraphicsApi::None)
    {
        FrameDriverInstance = std::make_unique<FrameDriver>(RuntimeLoop);
        FrameDriverInstance->SetTargetFps(Configuration.Runtime.TargetFps);
        FrameDriverInstance->SetShouldExit([this] {
            if (!Running)
                return true;
            // A signal the process host caught. Ownership sits there because
            // signals belong to the process, not to any one engine in it; this
            // only reads what it was handed.
            if (Configuration.Runtime.HostExitFlag != nullptr
                && *Configuration.Runtime.HostExitFlag != 0)
            {
                return true;
            }
            return ExitAfterFrames != 0
                && RuntimeLoop.GetCurrentFrame().WallTime.FrameIndex >= ExitAfterFrames;
        });
        if (Configuration.Console.CommandFd >= 0)
            CommandFeed = std::make_unique<ConsoleLineFeed>(Configuration.Console.CommandFd);
        Initialized = true;
        return true;
    }

    PlatformState = std::make_unique<PlatformServices>(logging);
    // Gamepads are optional hardware: a system with no pad subsystem still
    // runs, it just never reports one.
    GamepadCaptureState = std::make_unique<SdlGamepadCapture>();
    if (!GamepadCaptureState->IsAvailable())
        logging.GetLogger<Engine>().Info("gamepad subsystem unavailable; pads will not be read");

    SdlWindow* window = PlatformState->CreatePrimaryWindow(Configuration.Window);
    if (window == nullptr || !window->IsValid())
    {
        std::fprintf(stderr, "Failed to create Vulkan window.\n");
        return failInitialize();
    }

#ifndef SENCHA_ENABLE_VULKAN
    std::fprintf(stderr, "Vulkan graphics requested but Sencha was built without Vulkan.\n");
    return failInitialize();
#else
    if (Configuration.Window.GraphicsApi != WindowGraphicsApi::Vulkan)
    {
        std::fprintf(stderr, "Unsupported graphics API in EngineConfig.\n");
        return failInitialize();
    }

    auto& windows = PlatformState->Windows;
    GraphicsState = std::make_unique<GraphicsServices>(logging, Configuration, *window, windows);
    if (!GraphicsState->IsValid())
    {
        std::fprintf(stderr, "Failed to initialize Vulkan engine services.\n");
        return failInitialize();
    }
    // Before any feature is added, so every feature Setup sees the bundle.
    GraphicsState->MainRenderer.SetInstrumentation(&InstrumentationBundle);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    // A zero timestampPeriod means the device cannot timestamp; the pool
    // stays permanently inert and Gpu mode degrades to Counters behavior.
    GpuTimestampsPool = std::make_unique<GpuTimestampPool>();
    GpuTimestampsPool->Configure(
        GraphicsState->Device.GetDevice(),
        GraphicsState->PhysicalDevice.GetProperties().limits.timestampPeriod,
        GraphicsState->Frames.GetFramesInFlight());
    VulkanDebugLabels::Load(GraphicsState->Instance.GetInstance());
    PublishCaptureEnvironment();
#endif

    RuntimeLoop.SetSurfaceExtent(window->GetExtent());
    FrameDriverInstance = std::make_unique<FrameDriver>(RuntimeLoop);
    FrameDriverInstance->SetTargetFps(Configuration.Runtime.TargetFps);
    FrameDriverInstance->SetShouldExit([this] {
        if (!Running)
            return true;
        return ExitAfterFrames != 0
            && RuntimeLoop.GetCurrentFrame().WallTime.FrameIndex >= ExitAfterFrames;
    });

    Initialized = true;
    return true;
#endif
}

void Engine::Shutdown()
{
    if (!Initialized)
        return;

    // Systems may retain references into the simulation. Shut them down while
    // the unified world and backend services are still alive, then join task
    // lanes before destroying the entity world they may have targeted.
    EngineSystems.Shutdown();
    // Before the frame driver: the net phases hold a pointer to this, and a
    // session outliving the loop that pumps it is a session nothing drains.
    // The goodbye is what turns this quit into an immediate leave on the other
    // end instead of a peer that lingers until its timeout.
    if (NetState != nullptr)
        NetState->Disconnect("quit");
    NetState.reset();
    // Recipes are callables a game module registered. A game clears its own in
    // OnShutdown; this is the backstop, because the Engine's own destruction
    // can run after the module is unmapped and destroying the callable then
    // reaches into memory that is gone.
    SpawnRecipeState.Clear();
    FrameDriverInstance.reset();
    TaskQueueInstance.reset();
    FramePoolInstance.reset();
    RuntimeWorldState.reset();
#ifdef SENCHA_ENABLE_DEBUG_UI
    // The renderer owns the feature; only the borrowed view is cleared here.
    DebugOverlayFeature = nullptr;
    PendingDebugPanels.clear();
#endif
#ifdef SENCHA_ENABLE_VULKAN
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    // Query pools die before the device they were created from. ~Renderer
    // has already waited for device idle by the time GraphicsState resets,
    // but the pools are engine members, so their teardown is explicit.
    if (GpuTimestampsPool != nullptr && GraphicsState != nullptr
        && GraphicsState->Device.GetDevice() != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(GraphicsState->Device.GetDevice());
        GpuTimestampsPool->Destroy();
    }
    GpuTimestampsPool.reset();
    InstrumentationBundle = RenderInstrumentation{};
    ActiveProfileMode = RenderProfileMode::Off;
    PendingProfileMode = RenderProfileMode::Off;
#endif
    GraphicsState.reset();
#endif
    GamepadCaptureState.reset();
    PlatformState.reset();
    CaptionState.reset();
    AudioState.reset();
    ConsoleState.reset();
    DebugState.reset();
    LoggingState.Clear();
    FramePhasesRegistered = false;
    Initialized = false;
    Running = false;
}

DebugService& Engine::Debug()
{
    assert(DebugState && "Engine::Debug: valid only between Initialize and Shutdown");
    return *DebugState;
}

const DebugService& Engine::Debug() const
{
    assert(DebugState && "Engine::Debug: valid only between Initialize and Shutdown");
    return *DebugState;
}

AudioService& Engine::Audio()
{
    assert(AudioState && "Engine::Audio: valid only between Initialize and Shutdown");
    return *AudioState;
}

const AudioService& Engine::Audio() const
{
    assert(AudioState && "Engine::Audio: valid only between Initialize and Shutdown");
    return *AudioState;
}

CaptionRuntime& Engine::Captions()
{
    assert(CaptionState && "Engine::Captions: valid only between Initialize and Shutdown");
    return *CaptionState;
}

const CaptionRuntime& Engine::Captions() const
{
    assert(CaptionState && "Engine::Captions: valid only between Initialize and Shutdown");
    return *CaptionState;
}

ConsoleService& Engine::Console()
{
    assert(ConsoleState && "Engine::Console: valid only between Initialize and Shutdown");
    return *ConsoleState;
}

const ConsoleService& Engine::Console() const
{
    assert(ConsoleState && "Engine::Console: valid only between Initialize and Shutdown");
    return *ConsoleState;
}

RuntimeWorld& Engine::World()
{
    assert(RuntimeWorldState
           && "Engine::World: valid after runtime schema sealing and before Shutdown");
    return *RuntimeWorldState;
}

const RuntimeWorld& Engine::World() const
{
    assert(RuntimeWorldState
           && "Engine::World: valid after runtime schema sealing and before Shutdown");
    return *RuntimeWorldState;
}

PlatformServices& Engine::Platform()
{
    assert(PlatformState && "Engine::Platform: valid only when windowed, between Initialize and Shutdown");
    return *PlatformState;
}

const PlatformServices& Engine::Platform() const
{
    assert(PlatformState && "Engine::Platform: valid only when windowed, between Initialize and Shutdown");
    return *PlatformState;
}

PlatformServices* Engine::TryPlatform()
{
    return PlatformState.get();
}

const PlatformServices* Engine::TryPlatform() const
{
    return PlatformState.get();
}

#ifdef SENCHA_ENABLE_VULKAN
GraphicsServices& Engine::Graphics()
{
    assert(GraphicsState && "Engine::Graphics: valid only when windowed, between Initialize and Shutdown");
    return *GraphicsState;
}

const GraphicsServices& Engine::Graphics() const
{
    assert(GraphicsState && "Engine::Graphics: valid only when windowed, between Initialize and Shutdown");
    return *GraphicsState;
}

GraphicsServices* Engine::TryGraphics()
{
    return GraphicsState.get();
}

const GraphicsServices* Engine::TryGraphics() const
{
    return GraphicsState.get();
}
#endif

JobSystem& Engine::Jobs()
{
    assert(FramePoolInstance && "Engine::Jobs: valid only between Initialize and Shutdown");
    return *FramePoolInstance;
}

const JobSystem& Engine::Jobs() const
{
    assert(FramePoolInstance && "Engine::Jobs: valid only between Initialize and Shutdown");
    return *FramePoolInstance;
}

AsyncTaskQueue& Engine::Tasks()
{
    assert(TaskQueueInstance && "Engine::Tasks: valid only between Initialize and Shutdown");
    return *TaskQueueInstance;
}

const AsyncTaskQueue& Engine::Tasks() const
{
    assert(TaskQueueInstance && "Engine::Tasks: valid only between Initialize and Shutdown");
    return *TaskQueueInstance;
}

SessionParticipantAdmission Engine::AdmitLocalParticipant()
{
    if (!Configuration.Runtime.HasLocalPlayer || RuntimeWorldState == nullptr)
        return {};

    // On a client the authority owns every participant, this machine's person
    // included, and it arrives replicated. Admitting one here as well is the
    // second provider that used to leave somebody driving a body the authority
    // knew nothing about while the one it did know about walked alongside.
    if (NetState != nullptr && NetState->Role() == NetSessionRole::Client)
        return {};

    return ParticipantProjection.AdmitLocal(RuntimeWorldState->Entities());
}

SessionParticipantAdmission Engine::AdmitSimulatedParticipant(
    InputActionSourceId source)
{
    if (RuntimeWorldState == nullptr)
        return {};
    return ParticipantProjection.AdmitSimulated(RuntimeWorldState->Entities(),
                                                source);
}

ParticipantBodyChange Engine::RequestParticipantBody(EntityId participant)
{
    if (RuntimeWorldState == nullptr)
        return {};
    return ParticipantProjection.RequestBody(RuntimeWorldState->Entities(),
                                              participant);
}

ParticipantControlChange Engine::SetParticipantControlSubject(
    EntityId participant, EntityId subject)
{
    if (RuntimeWorldState == nullptr)
        return {};
    return ParticipantProjection.SetControlSubject(
        RuntimeWorldState->Entities(), participant, subject);
}

SessionParticipantRetirement Engine::RetireParticipant(EntityId participant)
{
    if (RuntimeWorldState == nullptr)
        return {};
    return ParticipantProjection.RetireParticipant(
        RuntimeWorldState->Entities(), participant);
}

SessionParticipantRetirement Engine::RetireLocalParticipant()
{
    if (RuntimeWorldState == nullptr)
        return {};

    auto& entities = RuntimeWorldState->Entities();
    return ParticipantProjection.RetireParticipant(
        entities, LocalParticipantOf(entities));
}

void Engine::ResetNetSessionState()
{
    ReplicationState.Reset();
    CVarPublisherState.Reset();
    PeerCommandState.Reset();
    NetStatsState.Reset();
    NetClockState.Reset();
    PredictionState.Reset();
    InterpolationState.Reset();
}

NetSession* Engine::CreateNetSession(INetTransport& transport)
{
    if (NetState != nullptr)
        return nullptr;
    NetState = std::make_unique<NetSession>(transport);
    ResetNetSessionState();
    return NetState.get();
}

void Engine::DestroyNetSession()
{
    NetState.reset();
    ResetNetSessionState();
}

DefaultRenderPipeline* Engine::GetRenderPipeline()
{
    return EngineSystems.Get<DefaultRenderPipeline>();
}

const DefaultRenderPipeline* Engine::GetRenderPipeline() const
{
    return EngineSystems.Get<DefaultRenderPipeline>();
}

int Engine::Run(Game& game)
{
    if (!Initialize())
        return 1;

    game.AttachEngine(*this);

    // Storage, scene serializers, and the replicated table are three registries
    // filled in one pass from one set of declarations. They used to be filled
    // from three lists that nothing forced to agree, which is a way of saying
    // that a component could have storage and no serializer, or a place on the
    // wire and no column to land in. Now each component is named once and its
    // own schema decides which of the three it belongs in.
    //
    // The engine goes first so a game module adds to a known vocabulary rather
    // than being responsible for seeding it, and so game components take
    // runtime indices and wire keys after the engine's. Clear first: Run may be
    // called again in the same process.
    ComponentSerializerRegistry& serializers = SceneSerializerRegistry;
    serializers.Clear();
    RuntimeComponentSchemaState = WorldComponentSchema{};
    ReplicationLayoutState = ReplicationLayout{};

    ComponentRegistrar engineComponents(
        &RuntimeComponentSchemaState, &serializers, &ReplicationLayoutState);
    RegisterEngineComponents(engineComponents);

    ComponentRegistrar gameComponents(
        &RuntimeComponentSchemaState, &serializers, &ReplicationLayoutState);
    game.OnRegisterComponents(gameComponents);
    // What the module registered, so shutdown can retract exactly that while
    // the module is still mapped. The game does not repeat the list to take it
    // back; a list repeated is a list that can disagree with itself.
    const std::span<const ComponentTypeId> added = gameComponents.AddedSerializers();
    GameSerializerTypes.assign(added.begin(), added.end());

    std::string missingRuntimeComponent;
    if (!RuntimeComponentSchemaCoversSerializers(
            RuntimeComponentSchemaState,
            serializers,
            &missingRuntimeComponent))
    {
        std::fprintf(
            stderr,
            "Runtime component schema is missing storage for serialized component '%s'.\n",
            missingRuntimeComponent.c_str());
        RetractGameComponents();
        RuntimeComponentSchemaState = WorldComponentSchema{};
        return 1;
    }

    // A replicated table that cannot be compiled is wrong for every session
    // this build would ever run, so it is reported here rather than discovered
    // later as a misread snapshot.
    if (ReplicationLayoutState.Error() != ReplicationLayoutError::None)
    {
        std::fprintf(
            stderr,
            "Replicated component table is invalid (%.*s): %s.\n",
            static_cast<int>(
                ReplicationLayoutErrorToString(ReplicationLayoutState.Error()).size()),
            ReplicationLayoutErrorToString(ReplicationLayoutState.Error()).data(),
            ReplicationLayoutState.ErrorDetail().c_str());
        RetractGameComponents();
        RuntimeComponentSchemaState = WorldComponentSchema{};
        return 1;
    }

    std::string missingReplicatedComponent;
    if (!RuntimeComponentSchemaCoversReplication(
            RuntimeComponentSchemaState,
            ReplicationLayoutState,
            &missingReplicatedComponent))
    {
        std::fprintf(
            stderr,
            "Runtime component schema is missing storage for replicated component '%s'.\n",
            missingReplicatedComponent.c_str());
        RetractGameComponents();
        RuntimeComponentSchemaState = WorldComponentSchema{};
        return 1;
    }

    RuntimeComponentSchemaState.Seal();
    ReplicationLayoutState.Seal();

    // What a client resumes simulating for itself, from the same table that
    // says what travels. Bound here rather than per session: the answer is a
    // fact of the build, and a session starting must not be the moment it is
    // first asked.
    PredictionState.Bind(ReplicationLayoutState);

    // Said once, at startup, because the alternative is finding out from a
    // value that will not stay where its owner put it. Which components are
    // declared predicted is a fact of the build, so this is decided before the
    // first frame rather than watched for.
    {
        std::vector<std::string_view> unresumed;
        CollectUnresumedPredictedComponents(ReplicationLayoutState, unresumed);
        Logger& log = LoggingState.GetLogger<Engine>();
        for (const std::string_view name : unresumed)
        {
            log.Warn("prediction: '{}' is declared Predicted, but re-running a "
                     "tick does not resume it. Its owner's copy will be put "
                     "back to the authority's last word at every snapshot and "
                     "not carried forward from there.",
                     name);
        }
    }

    assert(!RuntimeWorldState && "Engine::Run called with a live runtime world");
    RuntimeWorldState =
        std::make_unique<RuntimeWorld>(RuntimeComponentSchemaState);

    ConsoleService& console = Console();
    console.AdvancePhase(ConsolePhase::EngineReady);

    // Running from the start of the lifecycle, not from the first frame, so
    // RequestExit means something during startup: a host that cannot load what
    // it was told to load has to be able to decline to run, and headless that
    // is the difference between exiting and spinning forever with no window to
    // close. The frame loop's exit predicate reads this before its first frame.
    Running = true;

    GameStartupContext startup{
        .Config = Configuration,
    };
    game.OnStart(startup);
    console.AdvancePhase(ConsolePhase::GameLoaded);
    // Reported rather than discarded: these are the commands that decide what
    // this process is -- which map it loaded, which port it is hosting on, who
    // it is connecting to -- and a host with no overlay has no other way to
    // learn that one of them failed, or which port an ephemeral bind chose.
    LogConsoleResult(LoggingState.GetLogger<Engine>(),
                     console.ExecuteStartupScript(StartupScript));

    SystemRegisterContext registerSystems{
        .Config = Configuration,
        .Schedule = EngineSystems,
    };
    game.OnRegisterSystems(registerSystems);
    EngineSystems.Init();
    console.AdvancePhase(ConsolePhase::SystemsRegistered);

    CreateDebugOverlay();

    if (FrameDriverInstance != nullptr)
    {
        RegisterFramePhases(game);
        if (!FrameTraceOutputPath.empty())
        {
            FrameTraceStore = std::make_unique<ChromeJsonFrameTrace>();
            FrameDriverInstance->SetTrace(FrameTraceStore.get());
        }
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        // Arm the render capture for the whole run; records only append once the
        // mode latch makes Capture active (render.profile.mode capture), so this
        // is inert unless both the path and the mode are set.
        if (!RenderCaptureOutputPath.empty())
            RenderCaptureStore.Start(0);
#endif
        FrameDriverInstance->Run();
        if (FrameTraceStore != nullptr
            && !FrameTraceStore->WriteTo(FrameTraceOutputPath))
        {
            std::fprintf(stderr, "Failed to write frame trace to '%s'.\n",
                         FrameTraceOutputPath.c_str());
        }
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        // Re-stamped here because the map is not known at graphics init.
        PublishCaptureEnvironment();
        if (!RenderCaptureOutputPath.empty()
            && !EngineConsoleBuiltins::WriteRenderCapture(
                   RenderCaptureStore, Console().Registry(), RenderCaptureOutputPath,
                   nullptr))
        {
            std::fprintf(stderr, "Failed to write render capture to '%s'.\n",
                         RenderCaptureOutputPath.c_str());
        }
#endif
    }

    GameShutdownContext shutdown{
        .Config = Configuration,
    };
    game.OnShutdown(shutdown);

    // Symmetric teardown of OnRegisterComponents above: retract the game's
    // serializers while the module is still mapped (the host unloads it after Run
    // returns). A module-owned serializer left in the registry would be freed at
    // exit, after dlclose, against unmapped code.
    RetractGameComponents();

    // Game component entries contain concrete registration function pointers
    // instantiated in the game module. Clear them before Engine::Run returns and
    // the host is allowed to unmap that module. RuntimeWorldState remains alive
    // until Engine::Shutdown, which also occurs inside Application::Run while the
    // module is mapped.
    RuntimeComponentSchemaState = WorldComponentSchema{};
    return 0;
}

void Engine::ApplyPendingRenderProfileMode()
{
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    ActiveProfileMode = PendingProfileMode;
    GpuTimestampPool* gpuTimestamps = nullptr;
#ifdef SENCHA_ENABLE_VULKAN
    gpuTimestamps = GpuTimestampsPool.get();
#endif
    InstrumentationBundle = ResolveInstrumentationBundle(
        ActiveProfileMode, &FrameRenderStats, &RenderStatsRing, &FrameCpuScopes,
        gpuTimestamps, &RenderCaptureStore);
    if (InstrumentationBundle.Stats != nullptr)
    {
        FrameRenderStats = RenderStats{};
        FrameRenderStats.FrameIndex = ++RenderStatsFrameIndex;
        FrameCpuScopes.ResetFrame();
    }
#endif
}

void Engine::PublishCaptureEnvironment()
{
#if defined(SENCHA_ENABLE_RENDER_PROFILING) && defined(SENCHA_ENABLE_VULKAN)
    if (GraphicsState == nullptr)
        return;

    const VkPhysicalDeviceProperties& device =
        GraphicsState->PhysicalDevice.GetProperties();
    const auto version = [](std::uint32_t packed) {
        return std::to_string(VK_API_VERSION_MAJOR(packed)) + "."
             + std::to_string(VK_API_VERSION_MINOR(packed)) + "."
             + std::to_string(VK_API_VERSION_PATCH(packed));
    };

    RenderCaptureStore.SetEnvironment({
        { "gpu_name", device.deviceName },
        { "gpu_vendor_id", std::to_string(device.vendorID) },
        { "gpu_device_id", std::to_string(device.deviceID) },
        { "gpu_device_type", std::to_string(static_cast<int>(device.deviceType)) },
        { "gpu_driver_version", std::to_string(device.driverVersion) },
        { "vulkan_api_version", version(device.apiVersion) },
        { "validation_enabled",
          Configuration.Graphics.EnableValidation ? "true" : "false" },
        { "frames_in_flight",
          std::to_string(Configuration.Graphics.FramesInFlight) },
        { "scratch_bytes_per_frame",
          std::to_string(Configuration.Graphics.FrameScratchBytesPerFrame) },
        { "build_sha", SENCHA_BUILD_SHA },
        { "build_type", SENCHA_BUILD_TYPE },
        { "map", ConsoleState != nullptr ? ConsoleState->CurrentMap() : std::string{} },
    });
#endif
}

void Engine::PushRenderStatsFrame()
{
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (InstrumentationBundle.Stats != nullptr
        && InstrumentationBundle.StatsHistory != nullptr)
    {
        InstrumentationBundle.StatsHistory->Push(FrameRenderStats);
    }
    if (InstrumentationBundle.Capture != nullptr
        && InstrumentationBundle.Capture->IsRecording())
    {
        // The timing sample for this frame was pushed just before this call.
        if (const TimingFrameSample* timing = TimingData.Latest())
            InstrumentationBundle.Capture->Append(*timing, FrameRenderStats);
    }
#endif
}

void Engine::RetractGameComponents()
{
    for (ComponentTypeId type : GameSerializerTypes)
        (void)SceneSerializerRegistry.Remove(type);
    GameSerializerTypes.clear();
}

void Engine::CreateDebugOverlay()
{
#if defined(SENCHA_ENABLE_DEBUG_UI) && defined(SENCHA_ENABLE_VULKAN)
    // Opt-out for hosts that own their own ImGui frontend (the editors); one
    // process can hold only one ImGui context over a window.
    if (!Configuration.Console.UiEnabled)
        return;
    if (GraphicsState == nullptr || PlatformState == nullptr)
        return;
    SdlWindow* window = PlatformState->Windows.GetPrimaryWindow();
    if (window == nullptr)
        return;

    auto overlay = std::make_unique<ImGuiDebugOverlay>(
        *DebugState, *window, GraphicsState->Instance, GraphicsState->Frames);
    overlay->AddPanel<ConsolePanel>(DebugState->GetLogSink(), *ConsoleState);
    overlay->AddPanel<TimingPanel>(TimingData);
    // Registered once for the process; the session it reads comes and goes.
    overlay->AddPanel<NetStatsPanel>(NetState, NetStatsState, NetClockState,
                                     PredictionState, InterpolationState,
                                     ReplicationState, PeerCommandState,
                                     ConsoleState->Registry());
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    overlay->AddPanel<RenderStatsPanel>(
        ActiveProfileMode, RenderStatsRing, ConsoleState->Registry());
#endif
    for (auto& panel : PendingDebugPanels)
        overlay->AddPanel(std::move(panel));
    PendingDebugPanels.clear();
    DebugOverlayFeature = static_cast<ImGuiDebugOverlay*>(
        GraphicsState->MainRenderer.AddFeature(std::move(overlay)));
#endif
}

#ifdef SENCHA_ENABLE_DEBUG_UI
void Engine::AddDebugPanel(std::unique_ptr<IDebugPanel> panel)
{
    if (panel == nullptr)
        return;
    if (DebugOverlayFeature != nullptr)
        DebugOverlayFeature->AddPanel(std::move(panel));
    else
        PendingDebugPanels.push_back(std::move(panel));
}
#endif

void Engine::RegisterEngineConsoleBuiltins(ConsoleService& console, DebugService& debug)
{
    ConsoleRegistry& registry = console.Registry();
    EngineConsoleBuiltins::RegisterConsoleCVars(registry, debug, Configuration.Console);
    EngineConsoleBuiltins::RegisterRuntimeCVars(registry, RuntimeLoop, Configuration.Runtime);
    EngineConsoleBuiltins::RegisterFramePacingCVars(
        registry, Configuration.Runtime, FrameDriverInstance);
    EngineConsoleBuiltins::RegisterRunControlCVars(
        registry, ExitAfterFrames, FrameTraceOutputPath);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    EngineConsoleBuiltins::RegisterProfilingCVars(registry, PendingProfileMode);
    EngineConsoleBuiltins::RegisterCaptureCommands(
        registry, RenderCaptureStore, PendingProfileMode, RenderCaptureOutputPath);
#endif
    EngineConsoleBuiltins::RegisterHostCommands(console, [this] { RequestExit(); });
    registry.RegisterCommand({
        .Name = "participant_status",
        .Owner = "engine",
        .Usage = "participant_status",
        .Help = "Print participant, control, and session-projection state, then "
                "validate their invariants on demand.",
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string>) {
            ConsoleResult result;
            result.Info(RuntimeWorldState == nullptr
                ? "no runtime world"
                : FormatSessionParticipantStatus(
                      RuntimeWorldState->Entities()));
            return result;
        },
    });
    EngineConsoleBuiltins::ApplyConfigAssignments(console, Configuration.Console);
}
