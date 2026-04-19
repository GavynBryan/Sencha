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
    Services_.AddService<DefaultSceneBinding>();

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
        return false;
    }

#ifndef SENCHA_ENABLE_VULKAN
    std::fprintf(stderr, "Vulkan graphics requested but Sencha was built without Vulkan.\n");
    return false;
#else
    if (Config_.Window.GraphicsApi != WindowGraphicsApi::Vulkan)
    {
        std::fprintf(stderr, "Unsupported graphics API in EngineConfig.\n");
        return false;
    }

    if (!VulkanBootstrap::Install(Services_, Config_, *window, windows))
    {
        std::fprintf(stderr, "Failed to initialize Vulkan engine services.\n");
        return false;
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

    Systems_.Shutdown();
    Driver_.reset();
    Services_.Clear();
    FramePhasesRegistered_ = false;
    Initialized_ = false;
    Running_ = false;
}

void Engine::RegisterDefaultRenderScene(DefaultRenderScene scene)
{
    GetDefaultSceneBinding().RegisterScene(scene);
}

bool Engine::AddDefaultMeshRenderFeature(MeshService& meshes, MaterialStore& materials)
{
    return GetDefaultSceneBinding().AddMeshRenderFeature(Services_, meshes, materials);
}

RenderQueue& Engine::GetRenderQueue()
{
    return GetDefaultSceneBinding().GetRenderQueue();
}

const RenderQueue& Engine::GetRenderQueue() const
{
    return GetDefaultSceneBinding().GetRenderQueue();
}

CameraRenderData& Engine::GetCameraData()
{
    return GetDefaultSceneBinding().GetCameraData();
}

const CameraRenderData& Engine::GetCameraData() const
{
    return GetDefaultSceneBinding().GetCameraData();
}

DefaultSceneBinding& Engine::GetDefaultSceneBinding()
{
    return Services_.Get<DefaultSceneBinding>();
}

const DefaultSceneBinding& Engine::GetDefaultSceneBinding() const
{
    return Services_.Get<DefaultSceneBinding>();
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
