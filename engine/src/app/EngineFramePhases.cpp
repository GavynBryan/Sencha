#include <app/Engine.h>
#include <app/Game.h>
#include <input/SdlGamepadCapture.h>
#include <input/SdlInputCapture.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/FrameDriver.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformHistory.h>
#include <world/transform/TransformPropagation.h>

#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/ImGuiDebugOverlay.h>
#endif

#include <SDL3/SDL.h>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/TimingSampler.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindowService.h>
#endif

// Defined here rather than in Engine.cpp so the phase bodies -- which reach
// deep into graphics, platform, and schedule state -- stay in one translation
bool Engine::HasPresentation() const
{
#ifdef SENCHA_ENABLE_VULKAN
    return PlatformState != nullptr && GraphicsState != nullptr;
#else
    return false;
#endif
}

void Engine::RegisterFramePhases(Game& game)
{
    if (FramePhasesRegistered || FrameDriverInstance == nullptr)
        return;

    // Presentation first, and not for cosmetic reasons: the driver runs a
    // phase's callbacks in registration order, and ResolveLifecycle is the one
    // phase both halves claim. The window observations have to land before the
    // transitions resolved from them.
    if (HasPresentation())
        RegisterPresentationFramePhases(game);
    else
        (void)game;

    RegisterSimulationFramePhases();

    FramePhasesRegistered = true;
}

// Everything a frame does that does not need a window: the async commit point,
// zone residency, the tick budget, the fixed ticks themselves, and the
// per-frame update. A headless host runs exactly this set.
void Engine::RegisterSimulationFramePhases()
{
    Engine& engine = *this;
    FrameDriver& driver = *FrameDriverInstance;
    auto& config = engine.Config();

    // The window half of this phase, when there is one, has already recorded
    // what it saw; this resolves the state machine from it. Headless there is
    // nothing to observe and the resolve is a no-op that keeps the frame state
    // reported correctly.
    driver.Register(FramePhase::ResolveLifecycle, [](PhaseContext& ctx) {
        ctx.Runtime->ResolveLifecycleTransitions();
    });

    driver.Register(FramePhase::DrainAsyncTasks, [&engine, &config](PhaseContext&) {
        AsyncDrainBudget budget;
        if (config.Runtime.AsyncCommitBudgetMs > 0.0)
        {
            budget.MaxTime = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double, std::milli>(config.Runtime.AsyncCommitBudgetMs));
        }
        engine.Tasks().DrainCompletions(budget);
        engine.World().FlushLifecycleRequests();
    });

    driver.Register(FramePhase::ZoneResidency, [&engine, &config](PhaseContext&) {
        RuntimeWorld& runtimeWorld = engine.World();
        ZoneResidencyContext residency{
            .Config = config,
            .Entities = runtimeWorld.Entities(),
            .Changes = runtimeWorld.BeginResidencyProcessing(),
        };
        engine.Schedule().RunZoneResidency(residency);
        runtimeWorld.FinalizeResidencyProcessing();
    });

    driver.Register(FramePhase::ScheduleTicks, [&engine, &config](PhaseContext& ctx) {
        const FrameZoneView& zones = engine.World().BuildFrameView();
        ctx.Zones = &zones;
        ctx.Runtime->ScheduleFixedTicks();

        // After the tick budget so the frame view is settled, and before the
        // ticks themselves consume what it produces.
        PreSimulateContext preSimulate{
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .Entities = *zones.Entities,
            .Partitions = zones.Logic,
        };
        engine.Schedule().RunPreSimulate(preSimulate);
    });

    driver.Register(FramePhase::Simulate, [&engine, &config](PhaseContext& ctx) {
        const FrameZoneView& zones = *ctx.Zones;
        ::World& entities = *zones.Entities;

        FixedLogicContext logic{
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Time = ctx.CurrentTick,
            .Entities = entities,
            .Partitions = zones.Logic,
        };
        engine.Schedule().RunFixedLogic(logic);

        PhysicsContext physics{
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Time = ctx.CurrentTick,
            .Entities = entities,
            .Partitions = zones.Physics,
        };
        engine.Schedule().RunPhysics(physics);

        PropagateTransforms(
            entities,
            zones.Logic,
            TransformPropagationDomain::Simulation,
            config.Runtime.TransformForceFullPropagation);

        PostFixedContext postFixed{
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Time = ctx.CurrentTick,
            .Entities = entities,
            .Partitions = zones.Logic,
        };
        engine.Schedule().RunPostFixed(postFixed);

        // Last thing in the tick, so the captured pose is the one this tick
        // finished with. A frame-wide discontinuity has no meaningful previous
        // pose to blend from, so it collapses every history instead.
        CaptureWorldTransformHistory(
            entities,
            zones.Logic,
            HasRuntimeFrameEvent(ctx.Runtime->GetCurrentFrame().Events,
                                 RuntimeFrameEventFlags::TemporalDiscontinuity));
    });

    driver.Register(FramePhase::Update, [&engine, &config](PhaseContext& ctx) {
        const RuntimeFrameSnapshot& rf = ctx.Runtime->GetCurrentFrame();
        const FrameZoneView& zones = *ctx.Zones;
        ::World& entities = *zones.Entities;

        FrameUpdateContext update{
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Input = *ctx.Input,
            .WallDeltaSeconds = static_cast<double>(rf.WallTime.Dt),
            .Presentation = rf.Presentation,
            .Entities = entities,
            .Partitions = zones.Logic,
        };
        engine.Schedule().RunFrameUpdate(update);

        AudioContext audio{
            .Config = config,
            .Runtime = *ctx.Runtime,
            .WallDeltaSeconds = static_cast<double>(rf.WallTime.Dt),
            .Presentation = rf.Presentation,
            .Entities = entities,
            .Partitions = zones.Audio,
        };
        engine.Schedule().RunAudio(audio);
    });

    driver.Register(FramePhase::EndFrame, [this, &engine, &config](PhaseContext& ctx) {
        const RuntimeFrameSnapshot& rf = ctx.Runtime->GetCurrentFrame();

        // PumpPlatform may request exit before lifecycle drain and frame-view
        // construction. That path has no simulation work to finalize.
        if (ctx.Zones == nullptr)
            return;

        const FrameZoneView& zones = *ctx.Zones;
        EndFrameContext endFrame{
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Presentation = rf.Presentation,
            .Entities = *zones.Entities,
            .Partitions = zones.Logic,
            .LifecycleOnly = rf.LifecycleOnly,
        };
        engine.Schedule().RunEndFrame(endFrame);

        engine.World().EndFrameView();
        ctx.Zones = nullptr;

        if (!rf.LifecycleOnly)
            return;

#ifdef SENCHA_ENABLE_VULKAN
        if (GraphicsState != nullptr)
        {
            TimingSampler::PushLifecycleFrame(
                engine.Timing(),
                rf,
                GraphicsState->Swapchain.GetState(),
                GraphicsState->Swapchain.GetRecreateCount());
        }
#endif
    });
}

// The phases that need a window, a swapchain, or a renderer. Compiled out
// entirely without Vulkan, and never registered when this process has no
// presentation services.
void Engine::RegisterPresentationFramePhases([[maybe_unused]] Game& game)
{
#ifdef SENCHA_ENABLE_VULKAN
    Engine& engine = *this;
    FrameDriver& driver = *FrameDriverInstance;

    auto& config = engine.Config();
    auto& windows = engine.Platform().Windows;
    auto& swapchain = engine.Graphics().Swapchain;
    auto& frames = engine.Graphics().Frames;
    auto& renderer = engine.Graphics().MainRenderer;
    const SdlWindowService::WindowId windowId = windows.GetPrimaryWindowId();

    driver.Register(FramePhase::PumpPlatform, [&engine, &game, &config, &windows, windowId](PhaseContext& ctx) {
        SdlInputCapture::BeginFrame(*ctx.Input);

        // Pads already plugged in at launch send no connection event, so the
        // first frame is where they are picked up.
        SdlGamepadCapture* gamepads = engine.GetGamepadCapture();
        if (gamepads != nullptr && !ctx.Input->GamepadConnected && gamepads->OpenCount() == 0)
            gamepads->OpenConnected(*ctx.Input);

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            windows.HandleEvent(event);

#ifdef SENCHA_ENABLE_DEBUG_UI
            // The overlay claims input before capture, not after: the grave
            // toggle always, and keyboard/mouse while the console is open. An
            // event folded into the InputFrame first would reach every gameplay
            // reader whatever the overlay then said about it.
            if (ImGuiDebugOverlay* overlay = engine.GetDebugOverlay();
                overlay != nullptr && overlay->ProcessSdlEvent(event))
            {
                continue;
            }
#endif

            SdlInputCapture::Accept(*ctx.Input, event);
            if (gamepads != nullptr)
                gamepads->Accept(*ctx.Input, event);

            PlatformEventContext eventCtx{
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

#ifdef SENCHA_ENABLE_DEBUG_UI
        // A press that began before the console opened would otherwise stay
        // held for as long as it is open, since its key-up is claimed above.
        if (ImGuiDebugOverlay* overlay = engine.GetDebugOverlay();
            overlay != nullptr && overlay->IsCapturingInput())
        {
            ctx.Input->ReleaseAllHeld();
        }
#endif

        if (windows.IsCloseRequested(windowId))
            ctx.Input->QuitRequested = true;
        if (config.Runtime.ExitOnEscape && ctx.Input->IsKeyDown(SDL_SCANCODE_ESCAPE))
            ctx.Input->QuitRequested = true;

        if (config.Runtime.TogglePauseOnF1
            && ctx.Input->ConsumeKeyPressed(SDL_SCANCODE_F1))
        {
            const bool wasPaused = ctx.Runtime->GetSimulationTimescale() == 0.0f;
            ctx.Runtime->SetSimulationTimescale(wasPaused ? 1.0f : 0.0f);
        }
    });

    driver.Register(FramePhase::ResolveLifecycle, [&windows, windowId](PhaseContext& ctx) {
        WindowExtent resizedExtent;
        if (windows.ConsumeResize(windowId, &resizedExtent))
            ctx.Runtime->NotifyResize(resizedExtent);

        const SdlWindowService::WindowState* windowState = windows.GetState(windowId);
        if (windowState != nullptr && windowState->Minimized)
            ctx.Runtime->NotifyMinimized();
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

    driver.Register(FramePhase::ExtractRenderPacket, [&engine, &config](PhaseContext& ctx) {
        // Before any extraction or recording reads the bundle, so one frame
        // sees exactly one profile mode.
        engine.ApplyPendingRenderProfileMode();

        const FrameZoneView& zones = *ctx.Zones;
        ::World& entities = *zones.Entities;

        PropagateTransforms(
            entities,
            zones.Visible,
            TransformPropagationDomain::Presentation,
            config.Runtime.TransformForceFullPropagation);

        RenderExtractContext extract{
            .Config = config,
            .Runtime = *ctx.Runtime,
            .PacketWrite = *ctx.PacketWrite,
            .PacketRead = *ctx.PacketRead,
            .Presentation = ctx.PacketWrite->Presentation,
            .Entities = entities,
            .Partitions = zones.Visible,
        };
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
            renderResult,
            engine.Instrumentation().GpuTimestamps,
            engine.Instrumentation().CpuScopes);
        // After the render phase, so pass-exit publishes are in the frame.
        engine.PushRenderStatsFrame();
    });
#endif
}
