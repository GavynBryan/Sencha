#include <gtest/gtest.h>

#include <app/Application.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <net/NetSession.h>
#include <net/UdpTransport.h>
#include <SDL3/SDL.h>

// The empty-session gate for the transport phase: two hosts, a real socket
// each, and a handshake that completes because the frame pumped it.
TEST(NetSessionFrame, TwoEnginesCompleteAHandshakeOverLoopback)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    // Transports before engines: the sessions the engines own reference them
    // through shutdown, where the engine says goodbye on the wire.
    UdpTransport hostTransport;
    UdpTransport clientTransport;

    EngineConfig hostConfig;
    hostConfig.Window.GraphicsApi = WindowGraphicsApi::None;
    hostConfig.Debug.ConsoleLogging = false;

    Engine hostEngine(hostConfig);
    ASSERT_TRUE(hostEngine.Initialize());

    NetSession* host = hostEngine.CreateNetSession(hostTransport);
    ASSERT_NE(host, nullptr);

    NetIdentity identity;
    identity.ModuleFingerprint = 1;
    identity.WorldIdentity = 2;
    identity.FixedTickRateMilliHz = 60000;
    ASSERT_TRUE(host->Host(0, identity));

    EngineConfig clientConfig = hostConfig;
    Engine clientEngine(clientConfig);
    ASSERT_TRUE(clientEngine.Initialize());

    NetSession* client = clientEngine.CreateNetSession(clientTransport);
    ASSERT_NE(client, nullptr);
    ASSERT_TRUE(client->Connect(host->LocalAddress(), identity));

    // Pumped through the sessions directly here: the phase wiring is asserted
    // separately below, and this half is about a real socket carrying a real
    // handshake between two independently constructed engines.
    for (int frame = 0; frame < 200 && !client->IsConnected(); ++frame)
    {
        const double now = frame * (1.0 / 60.0);
        (void)host->Pump(now);
        host->Flush(now);
        (void)client->Pump(now);
        client->Flush(now);
    }

    EXPECT_TRUE(client->IsConnected()) << "handshake did not complete over UDP";
    EXPECT_EQ(host->ConnectedPeers().size(), 1u);
    EXPECT_TRUE(client->LocalPeerId().IsValid());

    client->Disconnect("done");
    (void)host->Pump(100.0);
    EXPECT_EQ(host->ConnectedPeers().size(), 0u);
}

// Without a session the phases are one null check each, so a single-player
// frame is unchanged. This is the property that lets the phases be registered
// unconditionally.
TEST(NetSessionFrame, TheNetPhasesAreInertWithoutASession)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    EngineConfig config;
    config.Window.GraphicsApi = WindowGraphicsApi::None;
    config.Debug.ConsoleLogging = false;

    Engine engine(config);
    ASSERT_TRUE(engine.Initialize());
    EXPECT_EQ(engine.TryNet(), nullptr);
}

TEST(NetSessionFrame, ASessionIsDestroyedBeforeTheFrameLoopThatPumpsIt)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    EngineConfig config;
    config.Window.GraphicsApi = WindowGraphicsApi::None;
    config.Debug.ConsoleLogging = false;

    UdpTransport transport;
    {
        Engine engine(config);
        ASSERT_TRUE(engine.Initialize());
        ASSERT_NE(engine.CreateNetSession(transport), nullptr);
        // A second session must not replace the first silently.
        EXPECT_EQ(engine.CreateNetSession(transport), nullptr);
        engine.Shutdown();
        EXPECT_EQ(engine.TryNet(), nullptr);
    }
    // The transport outlives the engine here on purpose: the session holds a
    // reference to it, so tearing down in the other order is the use-after-free
    // this ordering exists to prevent.
    EXPECT_FALSE(transport.IsOpen());
}

namespace
{
    // Drives the test-owned host session from inside the client's frame, and
    // stops the loop once the handshake lands. The client's own session is
    // touched by nothing here: only PumpNet and FlushNet can advance it, which
    // is the whole point.
    struct HostPumpSystem
    {
        NetSession* Host = nullptr;
        NetSession* Client = nullptr;
        Engine* Owner = nullptr;
        int Frames = 0;

        void FrameUpdate(FrameUpdateContext&)
        {
            ++Frames;
            const double now = Frames * (1.0 / 240.0);
            if (Host != nullptr)
            {
                (void)Host->Pump(now);
                Host->Flush(now);
            }
            const bool connected = Client != nullptr && Client->IsConnected();
            if ((connected || Frames > 600) && Owner != nullptr)
                Owner->RequestExit();
        }
    };

    class FrameSessionClient final : public Game
    {
    public:
        FrameSessionClient(NetSession& host, const NetAddress& authority)
            : HostSession(host), Authority(authority) {}

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.TargetFps = 2000.0;
        }

        void OnStart(GameStartupContext&) override
        {
            Session = GetEngine().CreateNetSession(Transport);
            if (Session != nullptr)
            {
                NetIdentity identity;
                identity.ModuleFingerprint = 7;
                identity.WorldIdentity = 9;
                identity.FixedTickRateMilliHz = 60000;
                Started = Session->Connect(Authority, identity);
            }
        }

        void OnRegisterSystems(SystemRegisterContext& ctx) override
        {
            HostPumpSystem& system = ctx.Schedule.Register<HostPumpSystem>();
            system.Host = &HostSession;
            system.Client = Session;
            system.Owner = &GetEngine();
        }

        void OnShutdown(GameShutdownContext&) override
        {
            Connected = Session != nullptr && Session->IsConnected();
        }

        NetSession& HostSession;
        NetAddress Authority;
        UdpTransport Transport;
        NetSession* Session = nullptr;
        bool Started = false;
        bool Connected = false;
    };
}

// The test that would have caught the phases being declared, defined, and never
// registered. Everything above drives Pump and Flush directly, so it passes
// whether or not the frame ever calls them; this one never touches the client's
// session, so the handshake can only complete if the frame pumped it.
TEST(NetSessionFrame, TheFramePhasesPumpTheSession)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    UdpTransport hostTransport;
    EngineConfig hostConfig;
    hostConfig.Window.GraphicsApi = WindowGraphicsApi::None;
    hostConfig.Debug.ConsoleLogging = false;

    Engine hostEngine(hostConfig);
    ASSERT_TRUE(hostEngine.Initialize());
    NetSession* host = hostEngine.CreateNetSession(hostTransport);
    ASSERT_NE(host, nullptr);

    NetIdentity identity;
    identity.ModuleFingerprint = 7;
    identity.WorldIdentity = 9;
    identity.FixedTickRateMilliHz = 60000;
    ASSERT_TRUE(host->Host(0, identity));

    Application app(0, nullptr);
    FrameSessionClient game(*host, host->LocalAddress());
    ASSERT_EQ(app.Run(game), 0);

    ASSERT_TRUE(game.Started) << "the client never opened a socket";
    EXPECT_TRUE(game.Connected)
        << "the frame phases did not pump the session";
    EXPECT_EQ(host->ConnectedPeers().size(), 1u);
}
