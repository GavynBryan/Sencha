#include <app/Engine.h>
#include <app/Game.h>
#include <input/SdlGamepadCapture.h>
#include <input/SdlInputCapture.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/FrameDriver.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformHistory.h>
#include <core/console/ConsoleService.h>
#include <net/NetCVarSync.h>
#include <net/NetConsoleCommands.h>
#include <net/NetSession.h>
#include <net/ReplicationSnapshot.h>
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
    RegisterNetFramePhases();

    FramePhasesRegistered = true;
}

// The two net phases. Registered whether or not this process will ever host or
// join: without a session they are one null check each, and conditional
// registration would mean a session could not be created after the phases were
// wired -- which is exactly when `host` and `connect` run.
void Engine::RegisterNetFramePhases()
{
    Engine& engine = *this;
    FrameDriver& driver = *FrameDriverInstance;

    // The two flags track the client's outcome across frames so admission and
    // loss are logged once each, at the transition. The console cannot do it:
    // admission lands several round trips after `connect` returns, and a
    // headless client has no console view anyone watches.
    driver.Register(FramePhase::PumpNet,
                    [&engine, wasClient = false, wasAdmitted = false](
                        PhaseContext& ctx) mutable {
        NetSession* session = engine.TryNet();
        NetApplyConsoleAuthority(engine.Console().Registry(), session);
        if (session == nullptr)
        {
            // Destroyed via the console's own `disconnect`, which already
            // reported the outcome.
            wasClient = false;
            wasAdmitted = false;
            return;
        }

        // Unscaled wall time. Timeouts, resends, and clock sync are about how
        // long a peer has actually been silent; a paused or time-scaled
        // simulation must not stretch a peer's timeout along with it.
        const double now = ctx.Runtime->GetCurrentFrame().WallTime.UnscaledElapsed;
        engine.ClearNetDeliveries();
        const std::vector<NetSession::Delivery> deliveries = session->Pump(now);

        // Everything that arrived, counted before anything decides what to do
        // with it: traffic a build refuses is still traffic it paid for, and a
        // rate that only counted accepted messages would hide exactly the
        // pathologies worth seeing.
        NetStats& traffic = engine.NetTraffic();
        traffic.Sample(now);
        for (const NetSession::Delivery& delivery : deliveries)
        {
            traffic.RecordIn(delivery.Payload.empty()
                                 ? NetTrafficKind::Other
                                 : NetTrafficKindOf(static_cast<NetPayloadKind>(
                                       delivery.Payload[0])),
                             delivery.Payload.size());
        }

        // Logged rather than left to the console: a headless host has nobody
        // watching a console view, and who joined and who left is the first
        // thing anyone asks a server.
        Logger& log = engine.Logging().GetLogger<Engine>();
        for (const NetPeerEvent& event : session->PeerEvents())
        {
            if (event.Kind == NetPeerEventKind::Joined)
            {
                log.Info("net: peer {} joined from {}",
                         event.Peer.Value, NetAddressToString(event.Address));
            }
            else
            {
                log.Info("net: peer {} left ({})", event.Peer.Value, event.Reason);
                // Their baseline goes with them: it describes what a peer that
                // will never receive anything again was told, and their queued
                // input describes ticks nobody will simulate.
                engine.Replication().ForgetPeer(event.Peer);
                engine.PeerCommands().ForgetPeer(event.Peer);
            }
        }

        const bool isClient = session->Role() == NetSessionRole::Client;
        const bool isAdmitted = isClient && session->IsConnected();
        if (isAdmitted && !wasAdmitted)
        {
            log.Info("net: admitted as peer {} by {}",
                     session->LocalPeerId().Value,
                     NetAddressToString(session->Authority()));
        }
        if (wasClient && !isClient)
        {
            // Refusal, timeout, or a kick; the reason names which.
            log.Info("net: session ended: {}",
                     session->JoinFailureReason().empty()
                         ? "disconnected"
                         : session->JoinFailureReason());
        }
        wasClient = isClient;
        wasAdmitted = isAdmitted;

        // Snapshots are applied here, before the tick that will read them, and
        // outside any query -- applying one creates and destroys entities.
        // Deliveries must be drained whatever this process does with them, or
        // the transport's buffers and the peers' channel state never advance.
        // A host receives commands but never snapshots, so it must not stop
        // here; only a client that is not yet admitted has nothing to do.
        if (isClient && !isAdmitted)
            return;

        ::World& world = engine.World().Entities();
        for (const NetSession::Delivery& delivery : deliveries)
        {
            if (delivery.Payload.empty())
                continue;

            // A session-owned cvar arriving. Engine business rather than the
            // game's, because the flag that says the session owns it is the
            // console's own.
            if (static_cast<NetPayloadKind>(delivery.Payload[0])
                == NetPayloadKind::CVar)
            {
                NetCVarUpdate update;
                if (!NetDecodeCVarUpdate(delivery.Payload, update)
                    || !NetApplyCVarUpdate(engine.Console().Registry(), update))
                {
                    log.Warn("net: refused a cvar update from the authority");
                }
                continue;
            }

            // A player's request arriving. Buffered here and fed on the tick
            // clock, because a frame that runs several ticks owes a remote
            // player as many ticks of input as it gives the local one.
            if (static_cast<NetPayloadKind>(delivery.Payload[0])
                == NetPayloadKind::Command)
            {
                if (!engine.PeerCommands().Receive(delivery.From, delivery.Payload))
                {
                    log.Warn("net: refused a command from peer {}",
                             delivery.From.Value);
                }
                continue;
            }

            // Anything else that is not a snapshot is the game's; it is kept
            // for this frame rather than interpreted here.
            if (static_cast<NetPayloadKind>(delivery.Payload[0])
                != NetPayloadKind::Snapshot)
            {
                engine.RetainNetDelivery(delivery);
                continue;
            }

            const SnapshotApplyResult applied =
                engine.Replication().Apply(delivery.Payload, world,
                                           engine.RuntimeComponents(),
                                           engine.ReplicatedComponents(),
                                           &engine.SpawnRecipes());
            if (!applied.Ok())
            {
                // A snapshot that will not decode means the authority is
                // sending something this build cannot read. Logged rather than
                // fatal: the session's own strike machinery decides what to do
                // about a peer that keeps doing it.
                log.Warn("net: refused a snapshot ({})",
                         SnapshotApplyErrorToString(applied.Error));
            }
        }
    });

    driver.Register(FramePhase::FlushNet, [&engine](PhaseContext& ctx) {
        NetSession* session = engine.TryNet();
        if (session == nullptr)
            return;

        // Queued before the flush, so state produced by the simulation that
        // just ran leaves in the same frame that produced it rather than
        // waiting a full frame to go out.
        NetStats& traffic = engine.NetTraffic();
        if (session->Role() == NetSessionRole::Host)
        {
            ::World& world = engine.World().Entities();
            const ReplicationRuntime::PublishStats published =
                engine.Replication().Publish(
                    *session, world, engine.ReplicatedComponents(),
                    world.CurrentFrame());
            traffic.RecordOut(NetTrafficKind::Snapshot, published.BytesQueued,
                              published.SnapshotsSent);

            // Cheap when nothing changed: it compares what each peer was last
            // told and sends only differences.
            const NetCVarPublisher::PublishStats cvars =
                engine.CVarPublisher().Publish(
                    *session, engine.Console().Registry());
            if (cvars.Updates > 0)
            {
                traffic.RecordOut(NetTrafficKind::CVar, cvars.BytesQueued,
                                  static_cast<std::uint32_t>(cvars.Updates));
            }
        }
        else if (session->Role() == NetSessionRole::Client)
        {
            // Queued after the ticks that resolved it, so the newest record a
            // command carries is this frame's rather than the previous one's.
            const std::size_t bytes = engine.PeerCommands().SendLocal(
                *session, engine.World().Entities());
            if (bytes > 0)
                traffic.RecordOut(NetTrafficKind::Command, bytes);
        }

        session->Flush(ctx.Runtime->GetCurrentFrame().WallTime.UnscaledElapsed);
    });
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
            // Routed through the console rather than set directly, so pausing
            // obeys whatever the session decided about timescale. Solo is
            // unchanged, a host pausing pauses the session, and a client is
            // refused -- which is the correct answer to one player trying to
            // stop everyone else's game.
            const bool wasPaused = ctx.Runtime->GetSimulationTimescale() == 0.0f;
            const ConsoleResult set = engine.Console().Registry().SetCVar(
                "time.timescale", wasPaused ? 1.0 : 0.0,
                ConsoleValueSource{ "pause key" }, ConsolePhase::EngineReady);
            if (!set.Succeeded())
            {
                engine.Logging().GetLogger<Engine>().Info(
                    "pause: {}",
                    set.Output.empty() ? "refused" : set.Output.front().Text);
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
