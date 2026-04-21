#include <app/EngineFramePhases.h>

#include <app/Engine.h>
#include <app/Game.h>
#include <input/SdlInputCapture.h>
#include <runtime/FrameDriver.h>
#include <world/transform/TransformPropagation.h>

#include <SDL3/SDL.h>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/TimingSampler.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <platform/SdlWindowService.h>
#endif

void RegisterDefaultEngineFramePhases(Engine& engine, Game& game, FrameDriver& driver)
{
#ifdef SENCHA_ENABLE_VULKAN
    auto& services = engine.Services();
    auto& config = engine.Config();
    auto& windows = services.Get<SdlWindowService>();
    auto& swapchain = services.Get<VulkanSwapchainService>();
    auto& frames = services.Get<VulkanFrameService>();
    auto& renderer = services.Get<Renderer>();
    const SdlWindowService::WindowId windowId = windows.GetPrimaryWindowId();

    driver.Register(FramePhase::PumpPlatform, [&engine, &game, &config, &windows, windowId](PhaseContext& ctx) {
        SdlInputCapture::BeginFrame(*ctx.Input);
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            windows.HandleEvent(event);
            SdlInputCapture::Accept(*ctx.Input, event);

            PlatformEventContext eventCtx{
                .EngineInstance = engine,
                .Config = config,
                .Event = event,
            };
            game.OnPlatformEvent(eventCtx);
            if (eventCtx.Handled)
                continue;

            if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
                ctx.Runtime->NotifyMinimized();
            else if (event.type == SDL_EVENT_WINDOW_RESTORED)
                ctx.Runtime->NotifyRestored(windows.GetExtent(windowId));
        }

        if (windows.IsCloseRequested(windowId))
            ctx.Input->QuitRequested = true;
        if (config.Runtime.ExitOnEscape && ctx.Input->IsKeyDown(SDL_SCANCODE_ESCAPE))
            ctx.Input->QuitRequested = true;

        if (config.Runtime.TogglePauseOnF1)
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

    driver.Register(FramePhase::ResolveLifecycle, [&windows, windowId](PhaseContext& ctx) {
        WindowExtent resizedExtent;
        if (windows.ConsumeResize(windowId, &resizedExtent))
            ctx.Runtime->NotifyResize(resizedExtent);

        const SdlWindowService::WindowState* windowState = windows.GetState(windowId);
        if (windowState != nullptr && windowState->Minimized)
            ctx.Runtime->NotifyMinimized();

        ctx.Runtime->ResolveLifecycleTransitions();
    });

    driver.Register(FramePhase::RebuildGraphics, [&swapchain, &frames, &renderer](PhaseContext& ctx) {
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

    driver.Register(FramePhase::ScheduleTicks, [&engine](PhaseContext& ctx) {
        ctx.Registries = engine.Schedule().BuildFrameView(engine.Zones());
        ctx.Runtime->ScheduleFixedTicks();
    });

    driver.Register(FramePhase::Simulate, [&engine, &config](PhaseContext& ctx) {
        FixedLogicContext logic{
            .EngineInstance = engine,
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .Time = ctx.CurrentTick,
            .Registries = ctx.Registries,
            .ActiveRegistries = ctx.Registries.Logic,
        };
        engine.Schedule().RunFixedLogic(logic);

        PhysicsContext physics{
            .EngineInstance = engine,
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .Time = ctx.CurrentTick,
            .Registries = ctx.Registries,
            .ActiveRegistries = ctx.Registries.Physics,
        };
        engine.Schedule().RunPhysics(physics);

        PropagateTransforms(ctx.Registries.Logic);

        PostFixedContext postFixed{
            .EngineInstance = engine,
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .Time = ctx.CurrentTick,
            .Registries = ctx.Registries,
            .ActiveRegistries = ctx.Registries.Logic,
        };
        engine.Schedule().RunPostFixed(postFixed);
    });

    driver.Register(FramePhase::Update, [&engine, &config](PhaseContext& ctx) {
        const RuntimeFrameSnapshot& rf = ctx.Runtime->GetCurrentFrame();
        FrameUpdateContext update{
            .EngineInstance = engine,
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .WallDeltaSeconds = static_cast<double>(rf.WallTime.Dt),
            .Presentation = rf.Presentation,
            .Registries = ctx.Registries,
            .ActiveRegistries = ctx.Registries.Logic,
        };
        engine.Schedule().RunFrameUpdate(update);

        AudioContext audio{
            .EngineInstance = engine,
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .Presentation = rf.Presentation,
            .Registries = ctx.Registries,
            .ActiveRegistries = ctx.Registries.Audio,
        };
        engine.Schedule().RunAudio(audio);
    });

    driver.Register(FramePhase::ExtractRenderPacket, [&engine, &config](PhaseContext& ctx) {
        RenderExtractContext extract{
            .EngineInstance = engine,
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .PacketWrite = *ctx.PacketWrite,
            .PacketRead = *ctx.PacketRead,
            .Presentation = ctx.PacketWrite->Presentation,
            .Registries = ctx.Registries,
            .ActiveRegistries = ctx.Registries.Visible,
        };
        PropagateTransforms(ctx.Registries.Visible);
        engine.Schedule().RunExtractRender(extract);
    });

    driver.Register(FramePhase::Render, [&engine, &windows, windowId, &renderer, &frames, &swapchain](PhaseContext& ctx) {
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

        TimingSampler::PushRenderFrame(
            engine.Timing(),
            ctx.Runtime->GetCurrentFrame(),
            renderer.GetLastTiming(),
            frames.GetLastTiming(),
            swapchain.GetState(),
            swapchain.GetRecreateCount(),
            renderResult);
    });

    driver.Register(FramePhase::EndFrame, [&engine, &config, &swapchain](PhaseContext& ctx) {
        const RuntimeFrameSnapshot& rf = ctx.Runtime->GetCurrentFrame();
        EndFrameContext endFrame{
            .EngineInstance = engine,
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .Presentation = rf.Presentation,
            .Registries = ctx.Registries,
            .ActiveRegistries = ctx.Registries.Logic,
            .LifecycleOnly = rf.LifecycleOnly,
        };
        engine.Schedule().RunEndFrame(endFrame);

        if (!rf.LifecycleOnly)
            return;

        TimingSampler::PushLifecycleFrame(
            engine.Timing(),
            rf,
            swapchain.GetState(),
            swapchain.GetRecreateCount());
    });
#else
    (void)engine;
    (void)game;
    (void)driver;
#endif
}
