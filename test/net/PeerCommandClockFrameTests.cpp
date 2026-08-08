#include <gtest/gtest.h>

#include <app/Application.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <input/InputActionState.h>
#include <net/NetPlayerCommand.h>
#include <net/NetSession.h>
#include <net/UdpTransport.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <vector>

//=============================================================================
// Commands land on the authority's clock, through the real frame.
//
// The unit tests pin the estimator's arithmetic; this pins that the arithmetic
// is actually wired into a session. Nothing here calls SendLocal: a command can
// only reach the authority if the engine's own FlushNet phase produced one, and
// it can only carry an authority-clock tick if PumpNet fed the estimator first.
//
// The authority is a bare session driven by hand, deliberately started at a
// tick far from any count the client could reach on its own -- a client
// stamping its own ticks and a client stamping the authority's are otherwise
// indistinguishable when both machines start near zero, which is exactly the
// case a loopback test falls into by default.
//=============================================================================

namespace
{
    // Far enough from a joining client's own tick count that no plausible
    // local number lands near it.
    constexpr std::uint64_t kAuthorityEpoch = 4'000'000;

    NetIdentity ClockTestIdentity()
    {
        NetIdentity identity;
        identity.ModuleFingerprint = 11;
        identity.WorldIdentity = 22;
        identity.FixedTickRateMilliHz = 60000;
        return identity;
    }

    // Stands in for the input resolve pass: opens a record per tick so the
    // client has a history window to project into a command. A profile would
    // fill it with real actions; what travels here is the tick stamping.
    struct TickRecorderSystem
    {
        void FixedLogic(FixedLogicContext& ctx)
        {
            if (InputActionState* state =
                    ctx.Entities.TryGetResource<InputActionState>())
            {
                const std::span<InputActionValue> values =
                    state->BeginTick(ctx.Time.TickIndex);
                for (InputActionValue& value : values)
                    value = InputActionValue{};
            }
        }
    };

    // Drives the hand-held authority: advances its clock, answers the
    // handshake, and collects the command ticks that arrive.
    struct AuthorityDriverSystem
    {
        NetSession* Authority = nullptr;
        Engine* Owner = nullptr;
        std::vector<std::uint64_t>* CommandTicks = nullptr;
        std::uint64_t Tick = kAuthorityEpoch;
        int Frames = 0;

        void FrameUpdate(FrameUpdateContext&)
        {
            ++Frames;
            ++Tick;
            const double now = Frames * (1.0 / 240.0);

            // The authority names the clock; without this it publishes tick
            // zero forever and there is nothing for a client to align to.
            Authority->SetLocalTick(Tick);

            for (const NetSession::Delivery& delivery : Authority->Pump(now))
            {
                if (delivery.Payload.empty()
                    || static_cast<NetPayloadKind>(delivery.Payload[0])
                           != NetPayloadKind::Command)
                {
                    continue;
                }

                NetBitReader reader(
                    std::span<const std::byte>(delivery.Payload).subspan(1));
                NetPlayerCommand command;
                if (NetDecodePlayerCommand(reader, command))
                    CommandTicks->push_back(command.Records[0].Tick);
            }
            Authority->Flush(now);

            if (CommandTicks->size() >= 8 || Frames > 3000)
                Owner->RequestExit();
        }
    };

    class CommandClockGame final : public Game
    {
    public:
        CommandClockGame(NetSession& authority, const NetAddress& address)
            : Authority(authority), Address(address) {}

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.TargetFps = 2000.0;
        }

        void OnStart(GameStartupContext&) override
        {
            Engine& engine = GetEngine();
            World& world = engine.World().Entities();

            // What a client needs before it has anything to say: somewhere to
            // resolve input into, and one pawn whose aim the command carries.
            InputActionState& actions = world.AddResource<InputActionState>();
            actions.Configure(2);

            const EntityId pawn = world.CreateEntity();
            world.AddComponent<LookOrientation>(pawn, LookOrientation{});
            world.AddComponent<LocalLookControl>(pawn, {});

            NetSession* session = engine.CreateNetSession(Transport);
            if (session == nullptr)
                return;
            Joined = session->Connect(Address, ClockTestIdentity());
        }

        void OnRegisterSystems(SystemRegisterContext& ctx) override
        {
            ctx.Schedule.Register<TickRecorderSystem>();
            AuthorityDriverSystem& driver =
                ctx.Schedule.Register<AuthorityDriverSystem>();
            driver.Authority = &Authority;
            driver.Owner = &GetEngine();
            driver.CommandTicks = &Ticks;
        }

        void OnShutdown(GameShutdownContext&) override
        {
            LocalTick = GetEngine().TryNet() == nullptr
                            ? 0
                            : GetEngine().TryNet()->LocalTick();
            HadEstimate = GetEngine().NetClock().HasEstimate();
        }

        UdpTransport Transport;
        NetSession& Authority;
        NetAddress Address;
        bool Joined = false;
        bool HadEstimate = false;
        std::uint64_t LocalTick = 0;
        std::vector<std::uint64_t> Ticks;
    };
}

TEST(PeerCommandClockFrame, CommandsAreStampedOnTheAuthoritysClock)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    UdpTransport authorityTransport;
    NetSession authority(authorityTransport);
    ASSERT_TRUE(authority.Host(0, ClockTestIdentity()));

    Application app(0, nullptr);
    CommandClockGame game(authority, authority.LocalAddress());
    ASSERT_EQ(app.Run(game), 0);

    ASSERT_TRUE(game.Joined) << "the engine never dialed the authority";
    ASSERT_TRUE(game.HadEstimate)
        << "the pump phase never named the authority's clock";
    ASSERT_FALSE(game.Ticks.empty())
        << "the flush phase never sent a command";

    // The whole point: the tick a command asks for is a number on the
    // authority's clock, not on the client's own.
    for (std::uint64_t tick : game.Ticks)
    {
        EXPECT_GE(tick, kAuthorityEpoch)
            << "a command carried a tick from the client's own counter, which "
               "names nothing the authority can act on";
    }

    // And it asks for a tick still to come. A command that arrives for a tick
    // already simulated is input thrown away, every time.
    EXPECT_GT(game.Ticks.back(), game.LocalTick);
}
