#ifdef SENCHA_ENABLE_UI
#if defined(SENCHA_ENABLE_UI) && defined(SENCHA_ENABLE_VULKAN)
#include <render/feature/UiRenderFeature.h>
#endif
#include <app/OptionsPage.h>
#include <app/EngineVerbs.h>
#include <app/PauseMenu.h>
#include <app/ShellVerbs.h>
#include <authored/VerbBindingData.h>
#include <authored/WorldVocabulary.h>
#include <logic/VerbRelaySystem.h>
#include <abilities/AbilityKit.h>
#include <participant/ParticipantLifecycle.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentEntityIndex.h>
#include <core/assets/AssetLease.h>
#include <ui/UiService.h>
#endif
#include <app/Engine.h>
#include <app/SessionParticipantDiagnostics.h>
#include <app/EngineConsoleBuiltins.h>
#include <app/PauseInputSystem.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputRegistration.h>
#include <core/console/CVarArchive.h>
#include <net/NetConsoleCommands.h>
#include <app/Game.h>
#include <app/GameDataAssets.h>
#include <app/LevelCommands.h>
#include <audio/AudioService.h>
#include <audio/AudioSystem.h>
#include <navigation/NavigationSystem.h>
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
#include <runtime/spawn/NetPrefabSpawner.h>
#include <runtime/spawn/SceneSpawnService.h>
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
#include <debug/NavigationPanel.h>
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

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

namespace
{
    // The engine's own content root: the application shell's default documents
    // and the face they draw with.
    //
    // Appended to the configured roots rather than prepended, so it is a
    // fallback and not an override -- RuntimeContent::Mount gives the first
    // root that claims a virtual path ownership of it, so a game shipping its
    // own ui/pause.rml shadows this one by path alone.
    //
    // Empty when there is nothing to mount, which is the ordinary case for a
    // build that installed no content and for a test binary.
    std::filesystem::path EngineContentRoot()
    {
        const auto usable = [](const std::filesystem::path& candidate) {
            std::error_code ec;
            return !candidate.empty() && std::filesystem::is_directory(candidate, ec) && !ec;
        };

        // An override first, so a packaging layout this does not anticipate can
        // be pointed at without a rebuild.
        if (const char* override = std::getenv("SENCHA_ENGINE_CONTENT");
            override != nullptr && override[0] != '\0')
        {
            const std::filesystem::path path(override);
            if (usable(path))
                return path;
        }

        // Installed, beside the executable, next to where the templates land.
        if (const char* base = SDL_GetBasePath(); base != nullptr)
        {
            const std::filesystem::path installed =
                std::filesystem::path(base) / ".." / "share" / "sencha" / "content";
            if (usable(installed))
                return installed.lexically_normal();
        }

#ifdef SENCHA_ENGINE_CONTENT_DIR
        // In-tree. Defined only for a build from this source tree.
        if (const std::filesystem::path source(SENCHA_ENGINE_CONTENT_DIR); usable(source))
            return source;
#endif
        return {};
    }
}

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
        SpawnServiceState.reset();
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

    // Registered here rather than with the other render commands, which run
    // before graphics exist. Kept out of the module-facing headers on purpose:
    // a screenshot is a dev facility and not worth an ABI fingerprint change,
    // which would reject every game module built against the old one.
    ConsoleState->Registry().RegisterCommand({
        .Name = "render.screenshot",
        .Owner = "engine",
        .Usage = "render.screenshot <path.png> [frame]",
        .Help = "Write a rendered frame to a PNG. With a frame number, waits "
                "until the renderer has drawn that many -- the first frames of "
                "a run are a window appearing and assets still arriving, so an "
                "unattended capture should name a frame the scene has settled "
                "by. Unavailable when the surface offers no readback usage.",
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            ConsoleResult result;
            if (args.empty() || args.size() > 2)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("expected <path.png> [frame]");
                return result;
            }
            std::uint64_t atFrame = 0;
            if (args.size() == 2)
            {
                try
                {
                    atFrame = static_cast<std::uint64_t>(std::stoull(args[1]));
                }
                catch (const std::exception&)
                {
                    result.Status = ConsoleStatus::InvalidArguments;
                    result.Error("frame must be a non-negative integer");
                    return result;
                }
            }
            if (!GraphicsState->MainRenderer.CaptureFrame(args[0], atFrame))
            {
                result.Status = ConsoleStatus::ExecutionFailed;
                result.Error("this surface does not support reading frames back");
                return result;
            }
            result.Info(std::format("capture armed for frame {} -> {}", atFrame, args[0]));
            return result;
        },
    });
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
    // After the systems that hold them, before the World they read.
    EventDispatcherState.reset();
    QueryDispatcherState.reset();
    VerbDispatcherState.reset();
    // Before the frame driver: the net phases hold a pointer to this, and a
    // session outliving the loop that pumps it is a session nothing drains.
    // The goodbye is what turns this quit into an immediate leave on the other
    // end instead of a peer that lingers until its timeout.
    if (NetState != nullptr)
        NetState->Disconnect("quit");
    NetState.reset();
    FrameDriverInstance.reset();
    TaskQueueInstance.reset();
    FramePoolInstance.reset();
    // Before the world they borrow, and before the asset system whose scenes
    // the prefab spawner holds resident.
    NetPrefabState.reset();
    SpawnServiceState.reset();
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

NetPrefabSpawner& Engine::NetPrefabs()
{
    assert(NetPrefabState && "Engine::NetPrefabs before Engine::Run");
    return *NetPrefabState;
}

SceneSpawnService& Engine::Spawns()
{
    assert(SpawnServiceState
           && "Engine::Spawns: valid over the same span as Engine::World");
    return *SpawnServiceState;
}

LoadedLevel& Engine::Level()
{
    assert(LevelState.has_value()
           && "Engine::Level: valid from just before OnStart to just after OnShutdown");
    return *LevelState;
}

const LoadedLevel& Engine::Level() const
{
    assert(LevelState.has_value()
           && "Engine::Level: valid from just before OnStart to just after OnShutdown");
    return *LevelState;
}

bool Engine::InstantiateShellBindings()
{
    if (!ContentState.has_value() || RuntimeWorldState == nullptr
        || VerbDispatcherState == nullptr)
    {
        return false;
    }

    Logger& log = LoggingState.GetLogger<Engine>();
    RuntimeAssets& assets = ContentState->Assets();
    const AssetLease lease = assets.Assets.LoadLease(kShellBindingsAsset, AssetType::Data);
    if (!lease.IsValid())
    {
        log.Warn("vocabulary: '{}' did not load, so the shell keeps its native handlers",
                 kShellBindingsAsset);
        return false;
    }

    ShellBindingLease = DataAssetCacheHandle(&assets.DataAssets,
                                             DataAssetHandle::FromToken(lease.OpaqueToken()));
    std::vector<std::string> errors;
    ShellBindingSet.InstantiateFrom(assets.DataAssets, ShellBindingLease.GetToken(),
                                    ShellBindingEnvironment(), errors);
    for (const std::string& error : errors)
        log.Error("vocabulary: {}", error);

    // Both, or neither: a half-bound stock menu is a menu with a row that
    // silently does nothing.
    return ShellBindingSet.Find(kShellResumeBinding) != nullptr
        && ShellBindingSet.Find(kShellQuitBinding) != nullptr;
}

void Engine::RegisterAuthoredEventCVars()
{
    ConsoleRegistry& registry = Console().Registry();
    // The console outlives the dispatcher, so it is looked up on each change.
    (void)registry.RegisterCVar({
        .Name = "authored.events.drain_budget",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(AuthoredEventDispatcher::kDefaultBudget),
        .CurrentValue = static_cast<std::int64_t>(EventDispatcherState->BudgetPerDrain()),
        .Flags = CVarFlags::None,
        .Help = "Subscriber calls one tick's event drain may make before it stops. A chain still "
                "running after authored.events.quarantine_after exhausted drains is quarantined.",
        .Source = { "engine" },
        .Min = 1.0,
        .Max = 1048576.0,
        .OnChange = [this](const CVarChangeContext& ctx) {
            if (EventDispatcherState != nullptr)
                EventDispatcherState->SetBudget(
                    static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
        },
    });
    (void)registry.RegisterCVar({
        .Name = "authored.events.queue_capacity",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(AuthoredEventDispatcher::kDefaultCapacity),
        .CurrentValue = static_cast<std::int64_t>(EventDispatcherState->Capacity()),
        .Flags = CVarFlags::None,
        .Help = "How many announced events may wait for the next drain. Overflow refuses the "
                "announcement rather than dropping one already queued.",
        .Source = { "engine" },
        .Min = 1.0,
        .Max = 1048576.0,
        .OnChange = [this](const CVarChangeContext& ctx) {
            if (EventDispatcherState != nullptr)
                EventDispatcherState->SetCapacity(
                    static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
        },
    });
    (void)registry.RegisterCVar({
        .Name = "authored.events.quarantine_after",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(EventDispatcherState->QuarantineAfterDrains()),
        .CurrentValue = static_cast<std::int64_t>(EventDispatcherState->QuarantineAfterDrains()),
        .Flags = CVarFlags::None,
        .Help = "How many consecutive drains a chain of events may exhaust the budget in before "
                "it is quarantined as runaway work. Sustained work, not a proven cycle: a finite "
                "chain longer than this many budgets is cut off too.",
        .Source = { "engine" },
        .Min = 1.0,
        .Max = 1024.0,
        .OnChange = [this](const CVarChangeContext& ctx) {
            if (EventDispatcherState != nullptr)
                EventDispatcherState->SetQuarantineAfter(
                    static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
        },
    });
#ifdef NDEBUG
    constexpr bool trapByDefault = false;
#else
    constexpr bool trapByDefault = true;
#endif
    EventDispatcherState->SetTrapOnQuarantine(trapByDefault);
    (void)registry.RegisterCVar({
        .Name = "authored.events.trap_on_quarantine",
        .Owner = "engine",
        .Type = CVarType::Bool,
        .DefaultValue = trapByDefault,
        .CurrentValue = trapByDefault,
        .Flags = CVarFlags::None,
        .Help = "Whether quarantining a runaway event chain also stops a debug build where it "
                "happens.",
        .Source = { "engine" },
        .OnChange = [this](const CVarChangeContext& ctx) {
            if (EventDispatcherState != nullptr)
                EventDispatcherState->SetTrapOnQuarantine(std::get<bool>(ctx.NewValue));
        },
    });
}

VerbBindingEnvironment Engine::ShellBindingEnvironment() const
{
    VerbBindingEnvironment environment;
    if (RuntimeWorldState != nullptr)
        environment = MakeVerbBindingEnvironment(RuntimeWorldState->Entities());
    if (ContentState.has_value())
    {
        const RuntimeAssets& assets = ContentState->Assets();
        environment.Assets = &assets.Registry;
        environment.DataAssets = &assets.DataAssets;
    }
    return environment;
}

void Engine::PublishSimulationAuthority()
{
    if (RuntimeWorldState == nullptr)
        return;
    ::World& entities = RuntimeWorldState->Entities();
    SimulationAuthority& fact = entities.HasResource<SimulationAuthority>()
        ? entities.GetResource<SimulationAuthority>()
        : entities.AddResource<SimulationAuthority>();
    fact.Authoritative = NetState == nullptr || NetState->Role() != NetSessionRole::Client;
}

void Engine::RefreshShellBindings()
{
    if (!ContentState.has_value() || VerbDispatcherState == nullptr)
        return;
#ifdef SENCHA_ENABLE_UI
    // Whoever is playing at this machine is who a menu entry acts for.
    if (PauseMenuState != nullptr && RuntimeWorldState != nullptr)
        PauseMenuState->SetInstigator(LocalParticipantOf(RuntimeWorldState->Entities()));
#endif
    std::vector<std::string> errors;
    if (ShellBindingSet.Refresh(&ContentState->Assets().DataAssets, ShellBindingEnvironment(),
                                errors))
    {
        Logger& log = LoggingState.GetLogger<Engine>();
        for (const std::string& error : errors)
            log.Error("vocabulary: {}", error);
    }
}

void Engine::SyncShellSurface()
{
#ifdef SENCHA_ENABLE_UI
    if (UiState == nullptr || !ShellSurface.IsValid() || PlatformState == nullptr)
        return;
    SdlWindow* window = PlatformState->Windows.GetPrimaryWindow();
    if (window == nullptr)
        return;

    const WindowExtent extent = window->GetExtent();
    if (extent.Width == 0 || extent.Height == 0)
        return;   // minimised; laying out against nothing collapses the document

    UiState->SetSurfaceSize(ShellSurface, RenderExtent{ extent.Width, extent.Height });
    UiState->SetSurfaceScale(ShellSurface, window->GetDisplayScale());
#endif
}

void Engine::RequestExit(ExitSource source)
{
    if (!Running)
        return;

    // Already asked and deferred. Coalesced rather than re-offered, because the
    // handler has a dialog up: holding Alt+F4, or a window manager repeating a
    // close, would otherwise stack one confirmation per event.
    if (ExitPending)
        return;

    if (OnExitRequested && OnExitRequested(source) == ExitDecision::Defer)
    {
        ExitPending = true;
        return;
    }
    Running = false;
}

void Engine::ConfirmExit()
{
    if (!ExitPending)
        return;
    // Granted without consulting the handler again: it is the thing that asked.
    ExitPending = false;
    Running = false;
}

void Engine::CancelExit()
{
    ExitPending = false;
}

void Engine::StopImmediately()
{
    ExitPending = false;
    Running = false;
}

void Engine::SetPointerCaptured(bool captured)
{
    PointerCaptureRequested = captured;
    ApplyPointerCapture();
}

void Engine::ApplyPointerCapture()
{
    bool overlayCapturing = false;
#ifdef SENCHA_ENABLE_DEBUG_UI
    overlayCapturing = DebugOverlayFeature != nullptr && DebugOverlayFeature->IsCapturingInput();
#endif
    // The game's standing intent, and the two facts that can override it. The
    // request itself is never written here: a game that never asked for capture
    // must not acquire it when a menu closes, and one that did gets exactly
    // what it asked for back.
    const bool desired = PointerCaptureRequested && PrimaryWindowFocused && !overlayCapturing
                      && !PauseStateInstance.SuppressesPointerCapture();
    if (desired == PointerCaptureApplied)
        return;

    // No window to capture a pointer into on a headless host; the request is
    // still recorded so a query answers what the game asked for.
    if (PlatformState == nullptr)
        return;
    SdlWindow* window = PlatformState->Windows.GetPrimaryWindow();
    if (window == nullptr || window->GetHandle() == nullptr)
        return;
    SDL_SetWindowRelativeMouseMode(window->GetHandle(), desired);
    PointerCaptureApplied = desired;

    // Entering relative mode hides the cursor and takes it over; leaving puts it
    // back where the desktop kept it. Either way it has moved without the player
    // moving it, and the platform reports that the only way it reports movement
    // at all. Without this the first frame back from a menu turns the view by
    // however far the cursor had wandered across it -- which reads as the camera
    // snapping to the mouse, because that is exactly what it is doing.
    CaptureSettle.NotifyChanged();
}

#ifdef SENCHA_ENABLE_UI
UiService& Engine::Ui()
{
    assert(UiState != nullptr
           && "Engine::Ui: valid from just before OnStart to just after OnShutdown");
    return *UiState;
}

const UiService& Engine::Ui() const
{
    assert(UiState != nullptr
           && "Engine::Ui: valid from just before OnStart to just after OnShutdown");
    return *UiState;
}
#endif

RuntimeContent& Engine::Content()
{
    assert(ContentState.has_value()
           && "Engine::Content: valid from just before OnStart to just after OnShutdown");
    return *ContentState;
}

const RuntimeContent& Engine::Content() const
{
    assert(ContentState.has_value()
           && "Engine::Content: valid from just before OnStart to just after OnShutdown");
    return *ContentState;
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

    return ParticipantProjection.AdmitLocal(RuntimeWorldState->Entities(),
                                            NetState != nullptr);
}

SessionParticipantAdmission Engine::AdmitSimulatedParticipant(
    InputActionSourceId source)
{
    if (RuntimeWorldState == nullptr)
        return {};
    return ParticipantProjection.AdmitSimulated(RuntimeWorldState->Entities(),
                                                source, NetState != nullptr);
}

ParticipantBodyChange Engine::RequestParticipantBody(EntityId participant)
{
    if (RuntimeWorldState == nullptr)
        return {};
    return ParticipantProjection.RequestBody(RuntimeWorldState->Entities(),
                                              participant, NetState != nullptr);
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

void Engine::ProjectSessionStart()
{
    if (RuntimeWorldState == nullptr || NetState == nullptr)
        return;
    ParticipantProjection.ProjectSessionStart(RuntimeWorldState->Entities());
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
    SpawnServiceState = std::make_unique<SceneSpawnService>(
        *RuntimeWorldState, RuntimeComponentSchemaState, SceneSerializerRegistry,
        Tasks(), LoggingState);
    NetPrefabState = std::make_unique<NetPrefabSpawner>(
        *RuntimeWorldState, RuntimeComponentSchemaState, SceneSerializerRegistry,
        LoggingState);

    // The authored vocabulary, before anything can resolve a name against it.
    //
    // Declaring is not implementing. The catalog exists from here on, so
    // content mounted below and a game's own startup can name these operations
    // and be told when they do not exist; what each one does is bound further
    // down, once the owners -- the shell, the exit path, the game's own systems
    // -- are there to hold.
    {
        // Qualified: Engine::World() is the runtime world accessor, and names
        // the member before it names the type in here.
        ::World& entities = RuntimeWorldState->Entities();
        // The registries the hook declares into, before the hook: tags,
        // attributes, effects, abilities -- registries only, no queue, no
        // component, nothing that simulates. Every editor World installs these
        // too, so a name a module declares lands here exactly as it does there.
        // Movement is not among them: it is a feature a game opts into, and a
        // locomotion mode is declared once the game has, as the templates do.
        InstallAbilityKitVocabulary(entities);
        InstallAuthoredVocabulary(entities);
        VerbRegistry& verbs = *FindVerbRegistry(entities);
        (void)DeclareEngineVerbs(verbs);

        // Registration only: no entities, no engine state, nothing that starts
        // a service. The hook returns void, so what it got wrong is read off
        // the catalog afterwards rather than trusted to the module -- a process
        // whose content names a verb that was refused must not reach a frame.
        game.OnRegisterVocabulary(entities);

        if (const std::vector<std::string> errors = AuthoredInstallationErrors(entities);
            !errors.empty())
        {
            for (const std::string& error : errors)
                std::fprintf(stderr, "Vocabulary installation failed: %s\n", error.c_str());
            NetPrefabState.reset();
            SpawnServiceState.reset();
            RuntimeWorldState.reset();
            RetractGameComponents();
            RuntimeComponentSchemaState = WorldComponentSchema{};
            return 1;
        }

        // One dispatcher for this catalog, composed rather than discovered. A
        // game reaches it through Engine::TryVerbs to bind what it declared.
        VerbDispatcherState = std::make_unique<VerbDispatcher>(verbs);
        VerbDispatcherState->SetEntityIndex(entities.TryGetResource<PersistentEntityIndex>());
        QueryDispatcherState =
            std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(entities));
        EventDispatcherState = std::make_unique<AuthoredEventDispatcher>(
            *FindAuthoredEventRegistry(entities), LoggingState.GetLogger<AuthoredEventDispatcher>());
        EventDispatcherState->SetWorld(&entities);
        RegisterAuthoredEventCVars();
        // Authoritative until a session says otherwise, which is the answer
        // for every process a session never touches.
        if (!entities.HasResource<SimulationAuthority>())
            entities.AddResource<SimulationAuthority>();
    }

    // The content stack, before the game exists as far as content is concerned:
    // OnStart sees a mounted, published stack rather than assembling one. The
    // game's data-asset subtypes register first, because the scan classifies
    // .sdata by the subtypes that exist when it runs.
    // Last, so it is the fallback every root above it may shadow.
    if (const std::filesystem::path engineContent = EngineContentRoot(); !engineContent.empty())
        Configuration.Runtime.ContentRoots.push_back(engineContent.string());

    ContentState.emplace(*this, LoggingState.GetLogger<Engine>());
    RegisterGameDataAssets(game, ContentState->Assets());
    ContentState->Mount();
    ContentState->Publish(RuntimeWorldState->Entities());
    VerbDispatcherState->SetDataAssets(&ContentState->Assets().DataAssets);
    LevelState.emplace(*this, *ContentState, LoggingState.GetLogger<Engine>());

    // The player's own settings, before the shell that offers them and before
    // the saved values are applied: the shell asks which of them this host
    // registered to decide what its options page has rows for, and a saved
    // setting whose cvar does not exist yet is queued rather than applied --
    // a volume arriving one run late is the kind of thing nobody reports.
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(
        Console().Registry(),
        AudioState.get(),
        PlatformState != nullptr ? PlatformState->Windows.GetPrimaryWindow() : nullptr,
        RuntimeWorldState != nullptr ? &RuntimeWorldState->Entities() : nullptr);

#ifdef SENCHA_ENABLE_UI
    // After the content stack, because a screen leases out of it, and before
    // OnStart, so a game can open one from its startup hook.
    {
        RuntimeAssets& assets = ContentState->Assets();
        SDL_Window* const window = PlatformState != nullptr
            ? PlatformState->Windows.GetNativeHandle(
                  PlatformState->Windows.GetPrimaryWindowId())
            : nullptr;
        UiState = std::make_unique<UiService>(LoggingState, assets.Assets, assets.UiPackages,
                                              assets.Fonts, assets.Textures.get(), window);
    }

    // The application shell, composed here rather than by the game.
    //
    // A new Sencha game gets a working menu by being a Sencha game: it writes
    // no pause code, declares no action and registers no context. What it does
    // instead is edit the model -- rename an entry, add one, replace the
    // document -- which is a different thing from assembling the machinery.
    //
    // One surface for the window, because modality is arbitrated within a
    // surface: pages on a surface of their own would take focus from nothing
    // while the HUD they meant to block kept taking clicks.
    // Only where the host said it is an application. An editor or a tool has a
    // window and, with the engine's content mounted, a ready UI layer -- and
    // neither of those is a declaration that a player sits in front of it.
    if (Configuration.Runtime.ApplicationShell && UiState != nullptr && UiState->IsReady()
        && PlatformState != nullptr)
    {
        SdlWindow* window = PlatformState->Windows.GetPrimaryWindow();
        const WindowExtent extent = window != nullptr ? window->GetExtent() : WindowExtent{};
        ShellSurface = UiState->CreateSurface(
            "shell", RenderExtent{ extent.Width, extent.Height });

        PauseMenuState = std::make_unique<PauseMenu>(
            *UiState, ShellSurface, PauseStateInstance, BackRouterInstance);

        PauseMenuModel& menu = PauseMenuState->Model();
        // How you leave is a platform fact, so the host names it; the command
        // it runs is neutral. A host that cannot terminate passes nothing here
        // and the entry is simply absent rather than present and inert.
        menu.InstallDefaults("Exit to Desktop");
        // The engine's own options page: the settings this host can actually
        // apply, which is whichever of them registered a cvar above.
        OptionsState.InstallDefaults(Console().Registry());
        if (!OptionsState.Rows().empty())
            menu.SetOptionsPage("asset://ui/options.rml");

        (void)menu.SetHandler(kPauseOptions, [this](PauseMenuContext& ctx) {
            // The page carries its own content and its own answer to a row
            // being activated, so the menu never learns what a setting is.
            PauseMenu::Page page;
            page.Desc = OptionsState.Describe(ctx.Menu.Model().OptionsPage());
            page.Publish = [this](UiScreenHandle screen) {
                if (UiState != nullptr)
                    (void)UiState->SetRows(screen, UiRowsIdAt(0),
                                           OptionsState.Present(Console().Registry()));
            };
            page.Activate = [this](std::size_t row, const UiValue& value) {
                (void)OptionsState.Apply(Console().Registry(), row, value);
            };
            // Closing the page is the commit boundary. A setting nudged a dozen
            // times on the way to the one the player wanted is one write, not
            // a dozen -- which is why the store is saved here rather than from
            // a frame phase.
            page.Closed = [this] {
                if (SettingsStore != nullptr)
                    (void)SettingsStore->Save(Console().Registry());
            };
            ctx.Menu.Push(std::move(page));
        });
        // The stock entries' behaviour, from binding data.
        //
        // What Resume and Quit do is one authored record each, in the engine's
        // own shell.bindings asset, resolved against this World's catalog like
        // any other content. There is no switch here relating a command id to
        // an operation: the model holds the key, the binding names the verb,
        // and this composition knows neither.
        //
        // The operations are bound now because this is where their owners exist
        // -- the menu was constructed a few lines up, and the exit path is this
        // engine.
        ResumeOperation = std::make_unique<RuntimeResumeOperation>(*PauseMenuState);
        QuitOperation = std::make_unique<ApplicationQuitOperation>(*this);
        VerbRegistry& verbs = RuntimeWorldState->Entities().GetResource<VerbRegistry>();
        ResumeBinding =
            VerbDispatcherState->Bind(verbs.Find(kRuntimeResumeVerb), *ResumeOperation);
        QuitBinding = VerbDispatcherState->Bind(verbs.Find(kApplicationQuitVerb), *QuitOperation);

        PauseMenuState->SetVerbBindings(VerbDispatcherState.get(), &ShellBindingSet);
        if (InstantiateShellBindings())
        {
            (void)menu.SetBinding(kPauseResume, MakeVerbBindingKey(kShellResumeBinding));
            (void)menu.SetBinding(kPauseExit, MakeVerbBindingKey(kShellQuitBinding));
        }
        else
        {
            // No engine content to load them from, which is the same condition
            // that leaves the menu without a document to present. Native
            // handlers keep the shell working for a host assembled that way,
            // and the model still allows exactly one behaviour per entry.
            (void)menu.SetHandler(kPauseResume,
                                  [](PauseMenuContext& ctx) { ctx.Menu.RequestResume(); });
            (void)menu.SetHandler(kPauseExit,
                                  [this](PauseMenuContext&) { RequestExit(ExitSource::Menu); });
        }
    }
#endif

    ConsoleService& console = Console();
    RegisterLevelCommands(console, *this);

    console.AdvancePhase(ConsolePhase::EngineReady);

    // Saved settings land here on purpose: after the engine's own cvars exist,
    // and before the startup script runs. That ordering is the precedence --
    // a +set on the command line is applied later and therefore wins over what
    // the player saved, which in turn wins over the defaults.
    //
    // A name no cvar claims yet is queued rather than dropped, so a setting for
    // a cvar the game module registers in OnStart is applied when it appears.
    //
    // The host names the directory and the game names itself, and this is the
    // first point where both are known: the host configured before the game's
    // OnConfigure ran. No directory means no archive, so a default
    // configuration cannot reach a user's disk.
    if (!Configuration.Console.SettingsRoot.empty())
    {
        SettingsStore = std::make_unique<CVarArchive>(
            CVarArchive::FileFor(Configuration.Console.SettingsRoot, Configuration.App.Name));
        SettingsStore->Load(console.Registry(), ConsolePhase::EngineReady);
        for (const std::string& diagnostic : SettingsStore->LoadDiagnostics())
            LoggingState.GetLogger<Engine>().Warn("{}", diagnostic);
    }

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
    // Input mapping is the engine's, not an opt-in a game assembles.
    //
    // It moved here when backing out of gameplay became an application
    // operation: the shell reads a mapped action, so the mapper has to exist in
    // a process whose game registered nothing at all. Registered before the
    // game's hook so the ordering edges a game declares against it already have
    // something to point at.
    RegisterInputSystems(EngineSystems, ContentState->Assets().DataAssets, LoggingState,
                         &RuntimeLoop.GetDiscontinuityBus());
    // Placed relays are the engine's too: a scene that carries one fires it
    // through the same dispatcher the shell uses, in a process whose game
    // registered nothing at all. Zero relays cost an empty-queue check per tick.
    (void)RegisterVerbRelaySystem(EngineSystems, RuntimeWorldState->Entities(),
                                  *VerbDispatcherState, ContentState->Assets().Registry,
                                  ContentState->Assets().DataAssets, console.Registry(),
                                  LoggingState);
    // The shell's Back reader goes with the shell: a host that composes no
    // menu must not have Escape flipping a pause state nothing presents.
    if (Configuration.Runtime.ApplicationShell)
    {
        EngineSystems.Register<PauseInputSystem>(PauseStateInstance, BackRouterInstance);
        EngineSystems.After<PauseInputSystem, InputActionResolveSystem>();
    }

    // Every host gets navigation: zones that cooked none cost it an empty
    // index, and a game reaches the queries through Schedule().Get<>().
    EngineSystems.Register<NavigationSystem>(*RuntimeWorldState, &Jobs(),
                                             &ConsoleState->Registry());

    game.OnRegisterSystems(registerSystems);
    // Every place a game binds has run.
    for (const AuthoredQueryId query : QueryDispatcherState->Unanswered())
    {
        LoggingState.GetLogger<Engine>().Warn(
            "Authored query '{}' is declared but nothing answers it",
            QueryDispatcherState->Registry().Get(query)->Name);
    }
    // After the game's, so whatever ordering constraints it declared already
    // exist when these are added.
    ContentState->RegisterSystems(EngineSystems);
    EngineSystems.Init();
    console.AdvancePhase(ConsolePhase::SystemsRegistered);

#if defined(SENCHA_ENABLE_UI) && defined(SENCHA_ENABLE_VULKAN)
    // Authored UI draws for every host, without one having to assemble the
    // feature itself. Phase order puts it over the scene and under the debug
    // overlay regardless of when it is added, so this needs no dependency edge
    // -- an editor ordering its own chrome against it will, and that is what
    // kUiRenderFeatureId is for.
    if (UiState != nullptr && UiState->IsReady() && GraphicsState != nullptr)
    {
        RuntimeAssets& assets = ContentState->Assets();
        GraphicsState->MainRenderer.AddFeature(
            std::make_unique<UiRenderFeature>(*UiState, assets.Textures.get()));
    }
#endif

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

    // Before the game's hook, so a setting it changes during teardown is not
    // what decides whether the file is written -- and before anything the store
    // borrows goes away. A run that changed nothing writes nothing.
    if (SettingsStore != nullptr)
        (void)SettingsStore->Save(Console().Registry());

    // Admission closes before anything is torn down, the game's own hook
    // included: from here no producer can hand an operation a request it will
    // not live to run. The dispatcher itself stays until Engine::Shutdown has
    // shut the scheduled systems down, so an operation cancelling its queued
    // work there still has the dependency it reports through.
    if (VerbDispatcherState != nullptr)
        VerbDispatcherState->CloseAdmission();
    if (EventDispatcherState != nullptr)
        EventDispatcherState->CloseAdmission();

    GameShutdownContext shutdown{
        .Config = Configuration,
    };
    game.OnShutdown(shutdown);

    // The host's implementations, then the compiled bindings that named them.
    // The game's own hook ran above, which is where it gave back its tokens.
    ResumeBinding.Reset();
    QuitBinding.Reset();
    ResumeOperation.reset();
    QuitOperation.reset();
#ifdef SENCHA_ENABLE_UI
    if (PauseMenuState != nullptr)
        PauseMenuState->SetVerbBindings(nullptr, nullptr);
#endif
    ShellBindingSet.Clear();
    // Before the content stack: these leases reference a cache inside it, and
    // the World that holds the relay store outlives that stack.
    ShellBindingLease.Reset();
    DisconnectVerbRelays(RuntimeWorldState->Entities());
    VerbDispatcherState->SetDataAssets(nullptr);

#ifdef SENCHA_ENABLE_UI
    // Before the content stack goes. A screen holds asset leases, and a lease
    // outliving the cache it references detaches from a destroyed owner.
    if (UiState != nullptr)
        UiState->Shutdown();
#endif

    // Task captures borrow loaders, caches, and serializers. Release unfinished
    // work while all of those owners (including the level loader) still exist.
    // Keep the stopped queue addressable for the level's cancellation path.
    Tasks().Stop();

    // Content teardown, in the one order that works: the game has just released
    // every lease it held, so the consumers of the stack are disconnected, then
    // the subtype registrations -- function pointers into the game module -- are
    // withdrawn while it is still mapped, and only then does the stack go.
    // The level goes before the content it loaded through, and its detaches
    // run while the game's zone-residency systems are still registered.
    LevelState->Unload();
    LevelState.reset();
    ContentState->Disconnect(RuntimeWorldState->Entities());
    UnregisterGameDataAssets(game, ContentState->Assets());
    ContentState.reset();
#ifdef SENCHA_ENABLE_UI
    UiState.reset();
    // Saved above, before the game's shutdown hook. Dropped here so a second
    // Run starts from the file rather than from this run's synced revision.
    SettingsStore.reset();
#endif

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
    if (const NavigationSystem* navigation = EngineSystems.Get<NavigationSystem>();
        navigation != nullptr && RuntimeWorldState != nullptr)
        overlay->AddPanel<NavigationPanel>(*RuntimeWorldState, *navigation);
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
    EngineConsoleBuiltins::RegisterHostCommands(
        console, [this] { RequestExit(ExitSource::Console); });
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
