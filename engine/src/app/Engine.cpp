#include <app/Engine.h>
#include <app/EngineConsoleBuiltins.h>
#include <app/EngineFramePhases.h>
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
#include <jobs/ThreadPoolJobSystem.h>
#include <runtime/FrameDriver.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/GraphicsServices.h>
#endif

#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>

#include <cassert>
#include <cstdio>
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
    if (Configuration.Console.OpenOnStart)
        DebugState->Open();
    EngineSystems.Register<DefaultRenderPipeline>();

    // Audio backend + the system that drives scene AudioSourceComponents
    // (docs/audio/runtime.md). An invalid service (no device — CI, headless)
    // is non-fatal: the system no-ops, the engine runs silent.
    AudioState = std::make_unique<AudioService>(logging, Configuration.Audio);
    EngineSystems.Register<AudioSystem>(AudioState.get());

    // Caption state above raw playback (docs/audio/captions-and-dialogue.md).
    // No device dependency — always valid, headless included. CaptionSystem
    // registers after AudioSystem so voices started or swept this frame are
    // captioned/retired the same frame.
    CaptionState = std::make_unique<CaptionRuntime>(logging, Configuration.Captions);
    EngineSystems.Register<CaptionSystem>(CaptionState.get(), AudioState.get());
    auto failInitialize = [this]() {
        EngineSystems.Shutdown();
        FrameDriverInstance.reset();
        TaskQueueInstance.reset();
        FramePoolInstance.reset();
#ifdef SENCHA_ENABLE_VULKAN
        GraphicsState.reset();
#endif
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

    // Task threads block on IO, so they never compete with the frame. The
    // count is config: 1 suits room-scale streaming, open worlds raise it.
    TaskQueueInstance = std::make_unique<AsyncTaskQueue>(
        static_cast<uint32_t>(Configuration.Runtime.AsyncTaskThreadCount));

    const int configuredWorkers = Configuration.Runtime.JobWorkerCount;
    FramePoolInstance = std::make_unique<ThreadPoolJobSystem>(
        configuredWorkers < 0 ? ThreadPoolJobSystem::DefaultWorkerCount()
                              : static_cast<uint32_t>(configuredWorkers));

    if (Configuration.Window.GraphicsApi == WindowGraphicsApi::None)
    {
        Initialized = true;
        return true;
    }

    PlatformState = std::make_unique<PlatformServices>(logging);
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

    RuntimeLoop.SetSurfaceExtent(window->GetExtent());
    FrameDriverInstance = std::make_unique<FrameDriver>(RuntimeLoop);
    FrameDriverInstance->SetTimingHistory(&TimingData);
    FrameDriverInstance->SetTargetFps(Configuration.Runtime.TargetFps);
    FrameDriverInstance->SetShouldExit([this] { return !Running; });

    Initialized = true;
    return true;
#endif
}

void Engine::Shutdown()
{
    if (!Initialized)
        return;

    EngineSystems.Shutdown();
    FrameDriverInstance.reset();
    TaskQueueInstance.reset();
    FramePoolInstance.reset();
#ifdef SENCHA_ENABLE_VULKAN
    GraphicsState.reset();
#endif
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

DefaultRenderPipeline* Engine::GetRenderPipeline()
{
    return EngineSystems.Get<DefaultRenderPipeline>();
}

const DefaultRenderPipeline* Engine::GetRenderPipeline() const
{
    return EngineSystems.Get<DefaultRenderPipeline>();
}

void Engine::RegisterFramePhases(Game& game)
{
    if (FramePhasesRegistered || FrameDriverInstance == nullptr)
        return;

    RegisterDefaultEngineFramePhases(*this, game, *FrameDriverInstance);

    FramePhasesRegistered = true;
}

int Engine::Run(Game& game)
{
    if (!Initialize())
        return 1;

    // Bind once, before any hook, so lifecycle contexts carry data only.
    game.AttachEngine(*this);

    // Components before content: register the game's serializers (a module game
    // registers its own here) so the first scene load resolves them. Same hook
    // the editor calls standalone to edit scenes without running the game.
    game.OnRegisterComponents(DefaultComponentSerializerRegistry());

    ConsoleService& console = Console();
    console.AdvancePhase(ConsolePhase::EngineReady);

    GameStartupContext startup{
        .Config = Configuration,
    };
    game.OnStart(startup);
    console.AdvancePhase(ConsolePhase::GameLoaded);
    (void)console.ExecuteStartupScript(StartupScript);

    SystemRegisterContext registerSystems{
        .Config = Configuration,
        .Schedule = EngineSystems,
    };
    game.OnRegisterSystems(registerSystems);
    EngineSystems.Init();
    console.AdvancePhase(ConsolePhase::SystemsRegistered);

    if (FrameDriverInstance != nullptr)
    {
        RegisterFramePhases(game);
        Running = true;
        FrameDriverInstance->Run();
    }

    GameShutdownContext shutdown{
        .Config = Configuration,
    };
    game.OnShutdown(shutdown);
    return 0;
}

void Engine::RegisterEngineConsoleBuiltins(ConsoleService& console, DebugService& debug)
{
    ConsoleRegistry& registry = console.Registry();
    EngineConsoleBuiltins::RegisterConsoleCVars(registry, debug, Configuration.Console);
    EngineConsoleBuiltins::RegisterRuntimeCVars(registry, RuntimeLoop, Configuration.Runtime);
    EngineConsoleBuiltins::RegisterFramePacingCVars(registry, Configuration.Runtime, FrameDriverInstance);
    EngineConsoleBuiltins::RegisterHostCommands(console, [this] { RequestExit(); });
    EngineConsoleBuiltins::ApplyConfigAssignments(console, Configuration.Console);
}
