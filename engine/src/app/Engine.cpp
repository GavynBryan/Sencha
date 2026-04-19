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

#include <platform/SdlVideoService.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <platform/WindowCreateInfo.h>

#include <cstdio>
#include <utility>

namespace
{
    WindowCreateInfo BuildWindowCreateInfo(const EngineWindowConfig& config)
    {
        WindowCreateInfo info;
        info.Title = config.Title;
        info.Width = config.Width;
        info.Height = config.Height;
        info.Mode = config.Mode;
        info.GraphicsApi = config.GraphicsApi;
        info.Resizable = config.Resizable;
        info.Visible = config.Visible;
        return info;
    }

}

Engine::Engine(EngineConfig config)
    : Config_(std::move(config))
{
}

Engine::~Engine()
{
    Shutdown();
}

bool Engine::Initialize()
{
    if (Initialized_)
        return true;

    LoggingProvider& logging = Services_.GetLoggingProvider();
    if (Config_.Debug.ConsoleLogging)
        logging.AddSink<ConsoleLogSink>();

    DebugLogSink& debugLog = logging.AddSink<DebugLogSink>();
    Services_.AddService<DebugService>(logging, debugLog);
    Schedule_.Register<DefaultRenderPipeline>();
    auto failInitialize = [this]() {
        Schedule_.Shutdown();
        Driver_.reset();
        Services_.Clear();
        FramePhasesRegistered_ = false;
        Running_ = false;
        return false;
    };

    Runtime_.SetResizeSettleSeconds(Config_.Runtime.ResizeSettleSeconds);
    Runtime_.GetSimulationClock().SetFixedTickRate(Config_.Runtime.FixedTickRate);

    if (Config_.Window.GraphicsApi == WindowGraphicsApi::None)
    {
        Initialized_ = true;
        return true;
    }

    auto& video = Services_.AddService<SdlVideoService>(logging);
    auto& windows = Services_.AddService<SdlWindowService>(logging, video);

    SdlWindow* window = windows.CreateWindow(BuildWindowCreateInfo(Config_.Window));
    if (window == nullptr || !window->IsValid())
    {
        std::fprintf(stderr, "Failed to create Vulkan window.\n");
        return failInitialize();
    }

#ifndef SENCHA_ENABLE_VULKAN
    std::fprintf(stderr, "Vulkan graphics requested but Sencha was built without Vulkan.\n");
    return failInitialize();
#else
    if (Config_.Window.GraphicsApi != WindowGraphicsApi::Vulkan)
    {
        std::fprintf(stderr, "Unsupported graphics API in EngineConfig.\n");
        return failInitialize();
    }

    if (!VulkanBootstrap::Install(Services_, Config_, *window, windows))
    {
        std::fprintf(stderr, "Failed to initialize Vulkan engine services.\n");
        return failInitialize();
    }

    Runtime_.SetSurfaceExtent(window->GetExtent());
    Driver_ = std::make_unique<FrameDriver>(Runtime_);
    Driver_->SetTimingHistory(&Timing_);
    Driver_->SetTargetFps(Config_.Runtime.TargetFps);
    Driver_->SetShouldExit([this] { return !Running_; });

    Initialized_ = true;
    return true;
#endif
}

void Engine::Shutdown()
{
    if (!Initialized_)
        return;

    Schedule_.Shutdown();
    Driver_.reset();
    Services_.Clear();
    FramePhasesRegistered_ = false;
    Initialized_ = false;
    Running_ = false;
}

bool Engine::AddDefaultMeshRenderFeature(MeshService& meshes, MaterialStore& materials)
{
    DefaultRenderPipeline& pipeline = GetDefaultRenderPipeline();
    pipeline.SetAssetStores(meshes, materials);
    return pipeline.AddMeshRenderFeature(Services_);
}

RenderQueue& Engine::GetRenderQueue()
{
    return GetDefaultRenderPipeline().GetRenderQueue();
}

const RenderQueue& Engine::GetRenderQueue() const
{
    return GetDefaultRenderPipeline().GetRenderQueue();
}

CameraRenderData& Engine::GetCameraData()
{
    return GetDefaultRenderPipeline().GetCameraData();
}

const CameraRenderData& Engine::GetCameraData() const
{
    return GetDefaultRenderPipeline().GetCameraData();
}

DefaultRenderPipeline& Engine::GetDefaultRenderPipeline()
{
    return *Schedule_.Get<DefaultRenderPipeline>();
}

const DefaultRenderPipeline& Engine::GetDefaultRenderPipeline() const
{
    return *Schedule_.Get<DefaultRenderPipeline>();
}

void Engine::RegisterFramePhases(Game& game)
{
    if (FramePhasesRegistered_ || Driver_ == nullptr)
        return;

    RegisterDefaultEngineFramePhases(*this, game, *Driver_);

    FramePhasesRegistered_ = true;
}

int Engine::Run(Game& game)
{
    if (!Initialize())
        return 1;

    GameStartupContext startup{
        .EngineInstance = *this,
        .Config = Config_,
    };
    game.OnStart(startup);

    SystemRegisterContext registerSystems{
        .EngineInstance = *this,
        .Config = Config_,
        .Schedule = Schedule_,
    };
    game.OnRegisterSystems(registerSystems);
    Schedule_.Init();

    if (Driver_ != nullptr)
    {
        RegisterFramePhases(game);
        Running_ = true;
        Driver_->Run();
    }

    GameShutdownContext shutdown{
        .EngineInstance = *this,
        .Config = Config_,
    };
    game.OnShutdown(shutdown);
    return 0;
}
