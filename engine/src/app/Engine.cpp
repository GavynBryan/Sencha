#include <app/Engine.h>
#include <app/EngineFramePhases.h>
#include <app/Game.h>
#include <core/logging/ConsoleLogSink.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <runtime/FrameDriver.h>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/VulkanBootstrap.h>
#endif

#include <platform/SdlBootstrap.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>

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

    LoggingProvider& logging = ServiceRegistry.GetLoggingProvider();
    if (Configuration.Debug.ConsoleLogging)
        logging.AddSink<ConsoleLogSink>();

    DebugLogSink& debugLog = logging.AddSink<DebugLogSink>();
    ServiceRegistry.AddService<DebugService>(logging, debugLog);
    EngineSystems.Register<DefaultRenderPipeline>();
    auto failInitialize = [this]() {
        EngineSystems.Shutdown();
        FrameDriverInstance.reset();
        ServiceRegistry.Clear();
        FramePhasesRegistered = false;
        Running = false;
        return false;
    };

    RuntimeLoop.SetResizeSettleSeconds(Configuration.Runtime.ResizeSettleSeconds);
    RuntimeLoop.GetSimulationClock().SetFixedTickRate(Configuration.Runtime.FixedTickRate);

    if (Configuration.Window.GraphicsApi == WindowGraphicsApi::None)
    {
        Initialized = true;
        return true;
    }

    SdlWindow* window = SdlBootstrap::Install(ServiceRegistry, Configuration, logging);
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

    auto& windows = ServiceRegistry.Get<SdlWindowService>();
    if (!VulkanBootstrap::Install(ServiceRegistry, Configuration, *window, windows))
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
    ServiceRegistry.Clear();
    FramePhasesRegistered = false;
    Initialized = false;
    Running = false;
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

    GameStartupContext startup{
        .EngineInstance = *this,
        .Config = Configuration,
    };
    game.OnStart(startup);

    SystemRegisterContext registerSystems{
        .EngineInstance = *this,
        .Config = Configuration,
        .Schedule = EngineSystems,
    };
    game.OnRegisterSystems(registerSystems);
    EngineSystems.Init();

    if (FrameDriverInstance != nullptr)
    {
        RegisterFramePhases(game);
        Running = true;
        FrameDriverInstance->Run();
    }

    GameShutdownContext shutdown{
        .EngineInstance = *this,
        .Config = Configuration,
    };
    game.OnShutdown(shutdown);
    return 0;
}
