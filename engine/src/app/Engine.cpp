#include <app/Engine.h>
#include <app/Game.h>
#include <core/logging/ConsoleLogSink.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <input/SdlInputCapture.h>
#include <render/MeshRenderFeature.h>
#include <render/RenderExtractionSystem.h>
#include <runtime/FrameDriver.h>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanAllocatorService.h>
#include <graphics/vulkan/VulkanBootstrapPolicy.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeletionQueueService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <graphics/vulkan/VulkanSurfaceService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <graphics/vulkan/VulkanUploadContextService.h>
#include <vulkan/vulkan.h>
#endif

#include <platform/SdlVideoService.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <platform/WindowCreateInfo.h>

#include <SDL3/SDL.h>

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

#ifdef SENCHA_ENABLE_VULKAN
    bool IsVulkanServiceChainValid(ServiceHost& services)
    {
        return services.Get<VulkanInstanceService>().IsValid()
            && services.Get<VulkanSurfaceService>().IsValid()
            && services.Get<VulkanPhysicalDeviceService>().IsValid()
            && services.Get<VulkanDeviceService>().IsValid()
            && services.Get<VulkanQueueService>().IsValid()
            && services.Get<VulkanAllocatorService>().IsValid()
            && services.Get<VulkanUploadContextService>().IsValid()
            && services.Get<VulkanBufferService>().IsValid()
            && services.Get<VulkanImageService>().IsValid()
            && services.Get<VulkanSamplerCache>().IsValid()
            && services.Get<VulkanShaderCache>().IsValid()
            && services.Get<VulkanPipelineCache>().IsValid()
            && services.Get<VulkanDescriptorCache>().IsValid()
            && services.Get<VulkanFrameScratch>().IsValid()
            && services.Get<VulkanSwapchainService>().IsValid()
            && services.Get<VulkanFrameService>().IsValid()
            && services.Get<Renderer>().IsValid();
    }
#endif
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

    VulkanBootstrapPolicy policy;
    policy.AppName = Config_.App.Name;
    policy.EnableValidation = Config_.Graphics.EnableValidation;
    policy.RequiredQueues.Present = true;
    policy.RequiredInstanceExtensions = windows.GetRequiredVulkanInstanceExtensions();
    policy.RequiredDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    auto& instance = Services_.AddService<VulkanInstanceService>(logging, policy);
    auto& surface = Services_.AddService<VulkanSurfaceService>(logging, instance, *window);
    auto& physicalDevice =
        Services_.AddService<VulkanPhysicalDeviceService>(logging, instance, policy, &surface);
    auto& device = Services_.AddService<VulkanDeviceService>(logging, physicalDevice, policy);
    auto& queues = Services_.AddService<VulkanQueueService>(logging, device, physicalDevice, policy);
    auto& allocator =
        Services_.AddService<VulkanAllocatorService>(logging, instance, physicalDevice, device);
    auto& upload = Services_.AddService<VulkanUploadContextService>(logging, device, queues);

    const uint32_t framesInFlight = Config_.Graphics.FramesInFlight == 0
        ? 1u
        : Config_.Graphics.FramesInFlight;
    auto& deletionQueue =
        Services_.AddService<VulkanDeletionQueueService>(logging, framesInFlight);
    auto& buffers = Services_.AddService<VulkanBufferService>(logging, device, allocator, upload);
    auto& images =
        Services_.AddService<VulkanImageService>(logging, device, allocator, upload, deletionQueue);
    auto& samplers = Services_.AddService<VulkanSamplerCache>(logging, device);
    auto& shaders = Services_.AddService<VulkanShaderCache>(logging, device);
    auto& pipelines = Services_.AddService<VulkanPipelineCache>(logging, device, shaders);
    auto& descriptors =
        Services_.AddService<VulkanDescriptorCache>(logging, device, buffers, images);
    auto& scratch = Services_.AddService<VulkanFrameScratch>(
        logging, device, physicalDevice, buffers,
        VulkanFrameScratch::Config{ .FramesInFlight = framesInFlight });
    auto& swapchain = Services_.AddService<VulkanSwapchainService>(
        logging, device, physicalDevice, surface, queues, window->GetExtent());
    auto& frames = Services_.AddService<VulkanFrameService>(
        logging, device, queues, swapchain, deletionQueue, framesInFlight);

    Services_.AddService<Renderer>(
        logging, device, physicalDevice, queues, swapchain, frames, allocator, buffers,
        images, samplers, shaders, pipelines, descriptors, scratch, upload);

    if (!IsVulkanServiceChainValid(Services_))
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
    DefaultRenderScene_ = {};
    RenderQueue_.Reset();
    CameraData_ = {};
    Services_.Clear();
    FramePhasesRegistered_ = false;
    Initialized_ = false;
    Running_ = false;
}

void Engine::RegisterDefaultRenderScene(DefaultRenderScene scene)
{
    DefaultRenderScene_ = scene;
    if (DefaultRenderScene_.IsValid())
    {
        DefaultRenderScene_.PresentationTransforms->Reset(
            *DefaultRenderScene_.Transforms,
            *DefaultRenderScene_.Hierarchy,
            *DefaultRenderScene_.PropagationOrder);
    }
}

bool Engine::AddDefaultMeshRenderFeature(MeshService& meshes, MaterialStore& materials)
{
#ifdef SENCHA_ENABLE_VULKAN
    Renderer* renderer = Services_.TryGet<Renderer>();
    if (renderer == nullptr)
        return false;

    return renderer->AddFeature(std::make_unique<MeshRenderFeature>(
        RenderQueue_, meshes, materials, CameraData_)).IsValid();
#else
    (void)meshes;
    (void)materials;
    return false;
#endif
}

bool Engine::ExtractDefaultRenderScene(RenderExtractContext& ctx)
{
    if (!DefaultRenderScene_.IsValid())
        return false;

#ifdef SENCHA_ENABLE_VULKAN
    auto& swapchain = Services_.Get<VulkanSwapchainService>();

    DefaultRenderScene_.PresentationTransforms->BuildRenderSnapshot(
        *DefaultRenderScene_.Transforms,
        *DefaultRenderScene_.Hierarchy,
        *DefaultRenderScene_.PropagationOrder,
        static_cast<float>(ctx.Presentation.Alpha));

    if (!CameraRenderDataSystem::Build(
            *DefaultRenderScene_.ActiveCamera,
            *DefaultRenderScene_.Cameras,
            *DefaultRenderScene_.PresentationTransforms,
            swapchain.GetExtent(),
            CameraData_))
    {
        ctx.PacketWrite.Renderable = false;
        return true;
    }

    ctx.PacketWrite.Camera = CameraData_;
    ctx.PacketWrite.HasCamera = true;

    RenderQueue_.Reset();
    RenderExtractionSystem::Extract(
        *DefaultRenderScene_.PresentationTransforms,
        *DefaultRenderScene_.Renderers,
        *DefaultRenderScene_.Meshes,
        *DefaultRenderScene_.Materials,
        CameraData_,
        RenderQueue_);
    FrustumCullingSystem::Cull(CameraData_, RenderQueue_);
    RenderQueue_.SortOpaque();
    ctx.PacketWrite.Renderable = true;
    return true;
#else
    (void)ctx;
    return false;
#endif
}

void Engine::RegisterFramePhases(Game& game)
{
    if (FramePhasesRegistered_ || Driver_ == nullptr)
        return;

#ifdef SENCHA_ENABLE_VULKAN
    auto& windows = Services_.Get<SdlWindowService>();
    auto& swapchain = Services_.Get<VulkanSwapchainService>();
    auto& frames = Services_.Get<VulkanFrameService>();
    auto& renderer = Services_.Get<Renderer>();
    const SdlWindowService::WindowId windowId = windows.GetPrimaryWindowId();

    Driver_->Register(FramePhase::PumpPlatform, [this, &game, &windows, windowId](PhaseContext& ctx) {
        SdlInputCapture::BeginFrame(*ctx.Input);
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            windows.HandleEvent(event);

            PlatformEventContext eventCtx{
                .EngineInstance = *this,
                .Config = Config_,
                .Event = event,
            };
            game.OnPlatformEvent(eventCtx);
            if (eventCtx.Handled)
                continue;

            SdlInputCapture::Accept(*ctx.Input, event);

            if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
                ctx.Runtime->NotifyMinimized();
            else if (event.type == SDL_EVENT_WINDOW_RESTORED)
                ctx.Runtime->NotifyRestored(windows.GetExtent(windowId));
        }

        if (windows.IsCloseRequested(windowId))
            ctx.Input->QuitRequested = true;
        if (Config_.Runtime.ExitOnEscape && ctx.Input->IsKeyDown(SDL_SCANCODE_ESCAPE))
            ctx.Input->QuitRequested = true;

        if (Config_.Runtime.TogglePauseOnF1)
        {
            const bool wasPaused = ctx.Runtime->GetSimulationTimescale() == 0.0f;
            for (uint32_t sc : ctx.Input->KeysPressed)
            {
                if (sc == SDL_SCANCODE_F1)
                {
                    ctx.Runtime->SetSimulationTimescale(wasPaused ? 1.0f : 0.0f);
                    break;
                }
            }
        }
    });

    Driver_->Register(FramePhase::ResolveLifecycle, [this, &windows, windowId](PhaseContext& ctx) {
        WindowExtent resizedExtent;
        if (windows.ConsumeResize(windowId, &resizedExtent))
            ctx.Runtime->NotifyResize(resizedExtent);

        const SdlWindowService::WindowState* windowState = windows.GetState(windowId);
        if (windowState != nullptr && windowState->Minimized)
            ctx.Runtime->NotifyMinimized();

        ctx.Runtime->ResolveLifecycleTransitions();
    });

    Driver_->Register(FramePhase::RebuildGraphics, [this, &swapchain, &frames, &renderer](PhaseContext& ctx) {
        if (ctx.Runtime->ShouldRebuildSwapchain())
        {
            const WindowExtent rebuildExtent = ctx.Runtime->GetDesiredSwapchainExtent();
            ctx.Runtime->BeginSwapchainRebuild();
            if (swapchain.Recreate(rebuildExtent))
            {
                frames.ResetAfterSwapchainRecreate();
                renderer.NotifySwapchainRecreated();
                ctx.Runtime->CompleteSwapchainRebuild(rebuildExtent);
            }
            else
            {
                ctx.Runtime->FailSwapchainRebuild();
            }
        }
    });

    Driver_->Register(FramePhase::ScheduleTicks, [](PhaseContext& ctx) {
        ctx.Runtime->ScheduleFixedTicks();
    });

    Driver_->Register(FramePhase::Simulate, [this, &game](PhaseContext& ctx) {
        FixedUpdateContext update{
            .EngineInstance = *this,
            .Config = Config_,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .Time = ctx.CurrentTick,
        };
        game.OnFixedUpdate(update);
    });

    Driver_->Register(FramePhase::ExtractRenderPacket, [this, &game](PhaseContext& ctx) {
        RenderExtractContext extract{
            .EngineInstance = *this,
            .Config = Config_,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .PacketWrite = *ctx.PacketWrite,
            .PacketRead = *ctx.PacketRead,
            .Presentation = ctx.PacketWrite->Presentation,
        };
        game.OnExtractRender(extract);
        if (!extract.PacketWrite.Renderable && extract.AllowDefaultRenderScene)
            ExtractDefaultRenderScene(extract);
    });

    Driver_->Register(FramePhase::Render, [this, &windows, windowId, &renderer, &frames, &swapchain](PhaseContext& ctx) {
        if (!ctx.PacketWrite->Renderable)
            return;

        const RenderFrameResult renderResult = renderer.DrawFrameScheduled();
        if (renderResult == RenderFrameResult::SwapchainOutOfDate
            || renderResult == RenderFrameResult::SurfaceSuboptimal)
        {
            ctx.Runtime->SetSurfaceExtent(windows.GetExtent(windowId));
            ctx.Runtime->NotifySwapchainInvalidated();
        }
        else if (renderResult == RenderFrameResult::Failed)
        {
            ctx.Input->QuitRequested = true;
        }

        const RuntimeFrameSnapshot& rf = ctx.Runtime->GetCurrentFrame();
        const VulkanFrameTiming& vk = frames.GetLastTiming();
        const RendererFrameTiming& rt = renderer.GetLastTiming();
        const SwapchainState swap = swapchain.GetState();
        Timing_.Push(TimingFrameSample{
            .RawDtSeconds = rf.WallTime.UnscaledDt,
            .TickDtSeconds = rf.TickDtSeconds,
            .PresentationDtSeconds = rf.Presentation.DeltaSeconds,
            .InterpolationAlpha = rf.Presentation.Alpha,
            .RenderRecordSeconds = rt.RecordSeconds,
            .AcquireSeconds = vk.AcquireSeconds,
            .SubmitSeconds = vk.SubmitSeconds,
            .PresentSeconds = vk.PresentSeconds,
            .TotalFrameSeconds = rt.TotalSeconds,
            .FixedTicks = rf.FixedTicks,
            .LifecycleState = static_cast<int>(rf.State),
            .TemporalDiscontinuityReason = static_cast<int>(rf.DiscontinuityReason),
            .RuntimeEvents = static_cast<uint32_t>(rf.Events),
            .RenderResult = static_cast<int>(renderResult),
            .Presented = renderResult == RenderFrameResult::Presented,
            .LifecycleOnly = rf.LifecycleOnly,
            .SwapchainGeneration = swap.Generation,
            .SwapchainRecreateCount = swapchain.GetRecreateCount(),
            .SwapchainImageIndex = vk.ImageIndex,
            .SwapchainImageCount = swap.ImageCount,
            .PresentMode = static_cast<int>(swap.PresentMode),
            .SwapchainRecreated = HasRuntimeFrameEvent(
                rf.Events, RuntimeFrameEventFlags::SwapchainRecreated),
            .PresentationReset =
                rf.DiscontinuityReason != TemporalDiscontinuityReason::None,
        });
    });

    Driver_->Register(FramePhase::EndFrame, [this, &swapchain](PhaseContext& ctx) {
        const RuntimeFrameSnapshot& rf = ctx.Runtime->GetCurrentFrame();
        if (!rf.LifecycleOnly)
            return;

        const SwapchainState swap = swapchain.GetState();
        Timing_.Push(TimingFrameSample{
            .RawDtSeconds = rf.WallTime.UnscaledDt,
            .TickDtSeconds = rf.TickDtSeconds,
            .PresentationDtSeconds = rf.Presentation.DeltaSeconds,
            .InterpolationAlpha = rf.Presentation.Alpha,
            .FixedTicks = rf.FixedTicks,
            .LifecycleState = static_cast<int>(rf.State),
            .TemporalDiscontinuityReason = static_cast<int>(rf.DiscontinuityReason),
            .RuntimeEvents = static_cast<uint32_t>(rf.Events),
            .RenderResult = static_cast<int>(RenderFrameResult::SkippedMinimized),
            .Presented = false,
            .LifecycleOnly = true,
            .SwapchainGeneration = swap.Generation,
            .SwapchainRecreateCount = swapchain.GetRecreateCount(),
            .SwapchainImageCount = swap.ImageCount,
            .PresentMode = static_cast<int>(swap.PresentMode),
            .SwapchainRecreated = HasRuntimeFrameEvent(
                rf.Events, RuntimeFrameEventFlags::SwapchainRecreated),
            .PresentationReset =
                rf.DiscontinuityReason != TemporalDiscontinuityReason::None,
        });
    });
#endif

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
