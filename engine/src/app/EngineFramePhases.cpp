#include <app/Engine.h>
#include <app/Game.h>
#include <input/SdlGamepadCapture.h>
#include <input/SdlInputCapture.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/FrameDriver.h>
#include <world/RuntimeWorld.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <world/transform/TransformHistory.h>
#include <core/console/ConsoleService.h>
#include <movement/FreeLocomotionSystem.h>
#include <net/NetCVarSync.h>
#include <physics/CharacterMoverPool.h>
#include <physics/components/CharacterMoverLink.h>
#include <net/NetConsoleCommands.h>
#include <net/NetMessageRouter.h>
#include <net/NetOwnership.h>
#include <net/NetSession.h>
#include <net/ReplicationSnapshot.h>
#include <participant/LocalControl.h>
#include <physics/PhysicsStepSystem.h>
#include <prediction/PawnStateReplay.h>
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

// Console output reported where a host can see it.
//
// A windowed process shows this in its overlay. A dedicated host has no
// overlay, and a command whose result went nowhere is a command an operator has
// no way to know failed -- including the ones its own startup script ran.
void Engine::SetWorldStreaming(WorldPartitionRuntime* partition,
                               AsyncZoneLoader* loader)
{
    StreamedWorld = partition;
    StreamedWorldLoader = loader;
}

void Engine::LogConsoleResult(Logger& log, const ConsoleResult& result)
{
    for (const ConsoleOutputEntry& entry : result.Output)
    {
        switch (entry.Severity)
        {
        case ConsoleOutputSeverity::Error:   log.Error("{}", entry.Text); break;
        case ConsoleOutputSeverity::Warning: log.Warn("{}", entry.Text); break;
        case ConsoleOutputSeverity::Info:    log.Info("{}", entry.Text); break;
        }
    }
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
    {
        RegisterPresentationFramePhases(game);
    }
    else
    {
        (void)game;
        RegisterHostCommandPhase();
    }

    RegisterSimulationFramePhases();
    RegisterNetFramePhases();

    FramePhasesRegistered = true;
}

// A headless host's terminal, pumped where every other input source is.
//
// PumpPlatform means "bring the outside world into this frame before anything
// reacts to it" -- that is what it does for a window's events, and a line
// someone typed at a server is the same kind of arrival. A process with no
// descriptor configured registers nothing, so the phase stays empty exactly as
// it was.
void Engine::RegisterHostCommandPhase()
{
    if (CommandFeed == nullptr)
        return;

    FrameDriverInstance->Register(FramePhase::PumpPlatform, [this](PhaseContext&) {
        if (!CommandFeed->IsOpen())
            return;

        Logger& log = Logging().GetLogger<Engine>();
        for (const std::string& line : CommandFeed->Poll())
        {
            if (line.empty())
                continue;

            const ConsoleResult result =
                Console().ExecuteLine(line, ConsoleValueSource{ "stdin" });
            LogConsoleResult(log, result);
        }
    });
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

        // Published before the pump, so the keepalives and admissions that
        // leave inside it carry this frame's tick rather than the last one's.
        // An authority is the machine that defines the clock; a client sets it
        // too, harmlessly, so nothing has to branch on role to keep it fresh.
        const FixedSimulationLoop& simulation = ctx.Runtime->GetSimulationClock();
        session->SetLocalTick(simulation.GetTickIndex());

        // What this authority is serving, refreshed before the pump that hands
        // out admissions: a peer accepted during this Pump is told the map this
        // process has loaded as of now, rather than as of the last frame. The
        // console owns which map was loaded; the session owns what it announces;
        // this is the only place the two meet.
        if (session->Role() == NetSessionRole::Host)
            session->SetAnnouncedMap(engine.Console().CurrentMap());

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
                // Admission is what makes somebody a participant, and only the
                // authority performs it: a client receives players as
                // replicated state, so building one here as well would leave a
                // machine holding two representations of the same person.
                if (session->Role() == NetSessionRole::Host)
                {
                    (void)engine.ParticipantProjection.AdmitPeer(
                        engine.World().Entities(), event.Peer);
                }
            }
            else
            {
                log.Info("net: peer {} left ({})", event.Peer.Value, event.Reason);
                // Their baseline goes with them: it describes what a peer that
                // will never receive anything again was told, and their queued
                // input describes ticks nobody will simulate.
                engine.Replication().ForgetPeer(event.Peer);
                engine.PeerCommands().ForgetPeer(event.Peer);
                auto& entities = engine.World().Entities();
                // One owner releases the participant, driven subject, body,
                // ownership projection, and peer input source together.
                const SessionParticipantRetirement retired =
                    engine.ParticipantProjection.RetirePeer(entities, event.Peer);
                if (retired.Retirement.BodyReaped)
                {
                    log.Info("net: removed the pawn for peer {} that left",
                             event.Peer.Value);
                }
            }
        }

        const bool isClient = session->Role() == NetSessionRole::Client;
        const bool isAdmitted = isClient && session->IsConnected();

        if (isAdmitted)
        {
            // A player this process provided for itself before joining is now
            // somebody else's to simulate, and the authority's copy of this
            // person is on its way. Leaving it would leave a second body
            // standing where the player used to be.
            //
            // Cheap to ask every frame -- it walks the participants, of which
            // there is at most one marked local -- and it answers nothing at
            // all after the first time.
            (void)engine.RetireLocalParticipant();

            // Seeded from admission, then kept fresh by the snapshots below.
            // Only the seed comes from here: the keepalive's copy is refreshed
            // every few seconds, and feeding a stale number in every frame
            // would drag the estimate away from what snapshots just said.
            if (!engine.NetClock().HasEstimate())
            {
                engine.NetClock().Observe(session->AuthorityTick(),
                                          simulation.GetTickIndex(),
                                          session->RoundTripMicroseconds(),
                                          simulation.GetFixedDt());
            }

            // How much margin the authority wants on arriving input. It is the
            // machine doing the buffering, so it decides; the value reaches
            // here as a replicated cvar rather than as a second protocol field.
            if (const CVarMetadata* slack =
                    engine.Console().Registry().FindCVar("net.command_slack"))
            {
                if (const std::int64_t* ticks =
                        std::get_if<std::int64_t>(&slack->CurrentValue))
                {
                    engine.NetClock().SetSlackTicks(
                        static_cast<std::uint32_t>(std::max<std::int64_t>(0, *ticks)));
                }
            }

            // How often the authority speaks, for the same reason and by the
            // same route. Presenting a mirrored entity ahead of the newest
            // sample that can physically exist holds the last pose instead of
            // blending, so this term has to come from the machine that decides
            // it rather than be assumed to be every tick.
            if (const CVarMetadata* interval =
                    engine.Console().Registry().FindCVar("net.snapshot_interval"))
            {
                if (const std::int64_t* ticks =
                        std::get_if<std::int64_t>(&interval->CurrentValue))
                {
                    engine.Interpolation().SetSnapshotInterval(
                        static_cast<std::uint32_t>(std::max<std::int64_t>(1, *ticks)));
                }
            }
        }
        if (isAdmitted && !wasAdmitted)
        {
            log.Info("net: admitted as peer {} by {}",
                     session->LocalPeerId().Value,
                     NetAddressToString(session->Authority()));

            // A client that was never told what to load renders an empty world
            // and feels the authority's geometry through reconciliation, which
            // reads as rubber-banding rather than as the missing step it is.
            // Loaded through the console rather than by calling a game's map
            // handler directly, so the map a session brought in is recorded the
            // same way one typed at the console is.
            const std::string& announced = session->AnnouncedMap();
            const std::string& loaded = engine.Console().CurrentMap();
            if (announced.empty())
            {
                // An authority hosting before it loaded anything. It has nothing
                // to announce and does not say so later: a map loaded after this
                // point never reaches an already-admitted client.
            }
            else if (loaded.empty())
            {
                const ConsoleResult run = engine.Console().ExecuteTokens(
                    { "map", announced },
                    ConsoleValueSource{ "session admission" });
                if (run.Status != ConsoleStatus::Ok)
                {
                    log.Warn("net: could not load the announced map '{}'",
                             announced);
                }
            }
            else if (loaded != announced)
            {
                // Left as a report rather than a refusal. Whether a content
                // difference should end a session is the world-identity
                // question, and that is decided at the handshake or not at all.
                log.Error("net: the authority is serving '{}' but this process "
                          "loaded '{}'", announced, loaded);
            }
        }
        if (wasClient && !isClient)
        {
            // Refusal, timeout, or a kick; the reason names which.
            log.Info("net: session ended: {}",
                     session->JoinFailureReason().empty()
                         ? "disconnected"
                         : session->JoinFailureReason());
            // Whatever this machine was driving belonged to a session that no
            // longer exists. A timeout does not destroy the session, so without
            // this the client goes on predicting a pawn nothing will ever
            // correct, and holds the look control that stops it being given a
            // fresh one.
            (void)SetLocalControlSubject(engine.World().Entities(), EntityId{});
            engine.Prediction().SetPredicted(EntityId{});
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

            // The authority deciding which rooms this machine holds open. Only
            // recorded here; loading them is streaming's business and confirming
            // them waits until the world says they are attached.
            if (static_cast<NetPayloadKind>(delivery.Payload[0])
                == NetPayloadKind::ZoneScope)
            {
                NetZoneScopeUpdate update;
                if (NetDecodeZoneScopeUpdate(delivery.Payload, update))
                    engine.Replication().ApplyZoneScope(update);
                else
                    log.Warn("net: refused a zone scope update from the authority");
                continue;
            }

            // A peer reporting a room loaded. What opens that zone for its
            // snapshots, so a peer naming one nobody granted it is trying to be
            // sent a part of the world the authority did not decide it should
            // see.
            if (static_cast<NetPayloadKind>(delivery.Payload[0])
                == NetPayloadKind::ZoneAck)
            {
                ZoneId zone;
                if (!NetDecodeZoneAck(delivery.Payload, zone)
                    || !engine.Replication().AcknowledgeZone(delivery.From, zone))
                {
                    log.Warn("net: refused a zone ack from peer {}",
                             delivery.From.Value);
                    session->StrikePeer(delivery.From, "unowed zone ack");
                }
                continue;
            }

            // What the authority believes this machine holds. Dev-only, and a
            // mismatch is logged rather than acted on: it exists to catch a
            // replication defect while somebody is looking for one.
            if (static_cast<NetPayloadKind>(delivery.Payload[0])
                == NetPayloadKind::DesyncHash)
            {
                std::uint64_t reportedTick = 0;
                const NetDesyncResult desync = engine.Replication().CheckDesync(
                    delivery.Payload, world, engine.ReplicatedComponents(),
                    &engine.Interpolation(), session->LocalPeerId(),
                    &reportedTick);
                if (desync.Diverged > 0)
                {
                    log.Warn("net: desync at tick {}: {} of {} entities differ; "
                             "first is net {} -- try net_entity {}",
                             reportedTick, desync.Diverged, desync.Compared,
                             desync.FirstDiverged.Value,
                             desync.FirstDiverged.Value);
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
                    session->StrikePeer(delivery.From, "malformed command");
                }
                continue;
            }

            // A game's own message. Answered here, in the pump that answers
            // the engine's, rather than parked in a buffer for a system that
            // might read it -- which is what used to happen, to a buffer
            // nothing ever read.
            const std::uint8_t kind =
                static_cast<std::uint8_t>(delivery.Payload[0]);
            if (kind != static_cast<std::uint8_t>(NetPayloadKind::Snapshot))
            {
                const NetMessageContext message{
                    .From = delivery.From,
                    .Entities = world,
                    .Objects = &engine.Replication(),
                    .Body = std::span<const std::byte>(delivery.Payload)
                                .subspan(kNetPayloadKindBytes),
                };
                if (!engine.NetMessages().Route(session->Role(), kind, message))
                {
                    // A kind nothing answers and a kind whose handler said no
                    // are the same strike and entirely different problems: one
                    // sends somebody looking for a missing Bind, the other for
                    // why a request was rejected. Worth the extra lookup on a
                    // path that only runs when something already went wrong.
                    if (engine.NetMessages().IsBound(kind))
                    {
                        log.Warn("net: peer {} sent payload kind {} and it was "
                                 "refused", delivery.From.Value, kind);
                    }
                    else
                    {
                        log.Warn("net: nothing answered payload kind {} from "
                                 "peer {}", kind, delivery.From.Value);
                    }
                    session->StrikePeer(delivery.From, "unanswerable payload");
                }
                continue;
            }

            const SnapshotApplyResult applied =
                engine.Replication().Apply(delivery.Payload, world,
                                           engine.RuntimeComponents(),
                                           engine.ReplicatedComponents(),
                                           &engine.SpawnRecipes(),
                                           &engine.Prediction(),
                                           &engine.Interpolation());

            // Start again from what the authority did, then re-run the ticks it
            // has not answered. Both halves are necessary: the state alone
            // rewinds the player by a round trip, and the input alone is what
            // this machine already guessed with.
            if (applied.ReconcilePredicted)
            {
                PawnReplayRequest replay;
                replay.Entities = &world;
                replay.Schema = &engine.RuntimeComponents();
                replay.Prediction = &engine.Prediction();
                if (PhysicsStepSystem* physics =
                        engine.Schedule().Get<PhysicsStepSystem>())
                {
                    replay.Movers = &physics->GetCharacterMovers();
                }
                // The values the scheduled tick integrates under, off the
                // system that owns them: a replayed tick under different
                // gravity than the tick it re-runs is a disagreement this
                // machine would inject into the pawn every correction.
                if (const FreeLocomotionSystem* locomotion =
                        engine.Schedule().Get<FreeLocomotionSystem>())
                {
                    replay.Gravity = locomotion->GetGravity();
                    replay.UpAxis = locomotion->GetUpAxis();
                }
                replay.AckTick = applied.CommandAck;
                replay.FixedDeltaSeconds =
                    static_cast<float>(simulation.GetFixedDt());
                replay.Replay = engine.Prediction().IsEnabled();
                (void)ReplayPawnState(replay);
            }
            // Every snapshot is also a clock sample, and the freshest one
            // available: it leaves the authority stamped with the tick that
            // produced it, once a frame rather than once a keepalive.
            if (applied.Ok() && isAdmitted)
            {
                engine.NetClock().Observe(applied.Tick, simulation.GetTickIndex(),
                                          session->RoundTripMicroseconds(),
                                          simulation.GetFixedDt());
            }

            // An entity whose recipe this build does not have is alive, holds
            // the right state, and has nothing to draw it -- which reads as a
            // rendering bug from every direction except this one. Reported per
            // arrival rather than per frame: a recipe runs when an entity first
            // appears, so this is bounded by spawns and not by frame rate.
            // What the authority just said about ownership, turned into the one
            // fact this machine acts on. Here rather than in a system because
            // this is where the fact arrives and where structural work on
            // arriving state already happens: it settles before the frame's
            // first tick, so nothing has an ordering to declare against it, and
            // no tick runs having predicted a body this client no longer drives.
            if (isAdmitted)
            {
                engine.ParticipantProjection.ReconcileClientControl(
                    world, session->LocalPeerId(), engine.Prediction());
            }

            if (applied.RecipesMissing > 0)
            {
                log.Warn("net: {} spawn(s) named recipe {} and others this build "
                         "did not register; those entities arrived bare",
                         applied.RecipesMissing, applied.FirstMissingRecipe);
            }

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
            // Stamped with the simulation tick, not the frame counter: it is
            // the label a client compares its own prediction of that moment
            // against, and frames and ticks are not the same count.
            const ReplicationRuntime::PublishStats published =
                engine.Replication().Publish(
                    *session, world, engine.ReplicatedComponents(),
                    ctx.Runtime->GetSimulationClock().GetTickIndex(),
                    &engine.PeerCommands(), &engine.World());
            traffic.RecordOut(NetTrafficKind::Snapshot, published.BytesQueued,
                              published.SnapshotsSent);

            // What the authority believes each peer holds, after the snapshot
            // that told them. Off unless net.desync_interval says otherwise.
            const ReplicationRuntime::DesyncStats probes =
                engine.Replication().PublishDesync(
                    *session, engine.ReplicatedComponents(),
                    ctx.Runtime->GetSimulationClock().GetTickIndex());
            if (probes.Reports > 0)
            {
                traffic.RecordOut(NetTrafficKind::Desync, probes.BytesQueued,
                                  probes.Reports);
            }

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
            // Whatever the ring holds, exactly as it was captured. Nothing is
            // in it before the authority's clock has a name here, because a
            // record filed under a name the authority cannot place is one it
            // can never acknowledge.
            const std::size_t bytes = engine.PeerCommands().SendLocal(
                *session, engine.Prediction().Commands(),
                engine.Replication().AppliedAck());
            if (bytes > 0)
                traffic.RecordOut(NetTrafficKind::Command, bytes);

            // Rooms granted earlier that this machine has since finished
            // loading. Here rather than at the grant, because residency
            // processing runs between the pump that received it and this: a
            // zone that attached during this frame is confirmed in the same
            // frame it became real.
            const ReplicationRuntime::ZoneAckStats acked =
                engine.Replication().AcknowledgeResidentZones(*session,
                                                              engine.World());
            if (acked.Acks > 0)
                traffic.RecordOut(NetTrafficKind::Zone, acked.BytesQueued, acked.Acks);
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

    driver.Register(FramePhase::ZoneResidency, [&engine, &config](PhaseContext& ctx) {
        RuntimeWorld& runtimeWorld = engine.World();

        // Streaming first, so a room a grant asked for this frame reaches
        // residency processing in this one rather than the next.
        if (WorldPartitionRuntime* partition = engine.WorldStreaming())
        {
            engine.ZoneStreaming().Update(
                runtimeWorld.Entities(),
                LocalControlSubjectOf(runtimeWorld.Entities()), engine.TryNet(),
                engine.Replication(), *partition, &engine.NetTraffic());
            if (AsyncZoneLoader* loader = engine.WorldStreamingLoader())
            {
                partition->Update(
                    ctx.Runtime->GetCurrentFrame().WallTime.UnscaledDt,
                    *loader, runtimeWorld);
            }
        }

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

        // FixedTicks counts ticks already finished this frame, so the remainder
        // including this one is what a burst-splitting system needs.
        const RuntimeFrameSnapshot& frame = ctx.Runtime->GetCurrentFrame();
        const std::uint32_t ticksLeft =
            frame.Budget.TicksToRunThisFrame > frame.FixedTicks
                ? frame.Budget.TicksToRunThisFrame - frame.FixedTicks
                : 1u;

        FixedLogicContext logic{
            .Config = config,
            .Runtime = *ctx.Runtime,
            .Time = ctx.CurrentTick,
            .Entities = entities,
            .Partitions = zones.Logic,
            .TicksLeftInFrame = ticksLeft,
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
