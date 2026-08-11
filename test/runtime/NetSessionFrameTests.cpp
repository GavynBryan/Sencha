#include <gtest/gtest.h>

#include <app/Application.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <core/console/ConsoleService.h>
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

//=============================================================================
// A client is told which world to load
//
// The frame's half of the admission. The session carries the name; these prove
// the frame acts on it exactly once, and does not act when it should not.
//
// Every one of these runs a real engine whose only route to the map handler is
// the admitted transition -- nothing here calls it directly, so a handler that
// was never invoked is a frame that never wired it.
//=============================================================================

namespace
{
    // A client whose map handler counts what it is asked to load. `Preloaded`
    // stands in for a process launched with `+map`: it runs the same console
    // path before any session exists.
    class MapAnnouncementClient final : public Game
    {
    public:
        MapAnnouncementClient(NetSession& host, const NetAddress& authority,
                              std::string preloaded = {})
            : HostSession(host), Authority(authority),
              Preloaded(std::move(preloaded)) {}

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.TargetFps = 2000.0;
        }

        void OnStart(GameStartupContext&) override
        {
            GetEngine().Console().SetMapHandler(
                [this](std::string_view map) {
                    ++Loads;
                    LastLoaded = std::string(map);
                    ConsoleResult result;
                    result.Status = ConsoleStatus::Ok;
                    return result;
                });

            // Deferred, because OnStart runs before the phase a map command
            // needs. That is the same route a `+map` launch argument takes:
            // held until the game is loaded, then run, and in either case done
            // long before a session exists.
            if (!Preloaded.empty())
            {
                (void)GetEngine().Console().ExecuteTokens(
                    { "map", Preloaded }, ConsoleValueSource{ "test" }, true);
            }

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
            CurrentMap = GetEngine().Console().CurrentMap();
        }

        NetSession& HostSession;
        NetAddress Authority;
        std::string Preloaded;
        UdpTransport Transport;
        NetSession* Session = nullptr;
        bool Started = false;
        bool Connected = false;
        int Loads = 0;
        std::string LastLoaded;
        std::string CurrentMap;
    };

    // A host session outside any engine, so a client engine's frame is the only
    // thing under test.
    struct StandaloneHost
    {
        UdpTransport Transport;
        NetSession Session{ Transport };

        bool Start(std::string_view announced)
        {
            NetIdentity identity;
            identity.ModuleFingerprint = 7;
            identity.WorldIdentity = 9;
            identity.FixedTickRateMilliHz = 60000;
            if (!Session.Host(0, identity))
                return false;
            Session.SetAnnouncedMap(announced);
            return true;
        }
    };
}

TEST(NetSessionFrame, AJoiningClientLoadsTheWorldItWasAdmittedTo)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    StandaloneHost host;
    ASSERT_TRUE(host.Start("levels/box"));

    Application app(0, nullptr);
    MapAnnouncementClient game(host.Session, host.Session.LocalAddress());
    ASSERT_EQ(app.Run(game), 0);

    ASSERT_TRUE(game.Connected) << "the handshake never completed";
    EXPECT_EQ(game.Loads, 1)
        << "a client joined a session and was never told what to load, so it "
           "renders an empty world and feels the authority's geometry through "
           "reconciliation instead";
    EXPECT_EQ(game.LastLoaded, "levels/box");
    EXPECT_EQ(game.CurrentMap, "levels/box")
        << "the map was loaded without being recorded, so nothing downstream "
           "knows which world this process is in";
}

// A process launched with the right map already has it. Loading it again would
// tear down a world for no reason.
TEST(NetSessionFrame, AClientAlreadyInTheRightWorldDoesNotReloadIt)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    StandaloneHost host;
    ASSERT_TRUE(host.Start("levels/box"));

    Application app(0, nullptr);
    MapAnnouncementClient game(host.Session, host.Session.LocalAddress(),
                               "levels/box");
    ASSERT_EQ(app.Run(game), 0);

    ASSERT_TRUE(game.Connected);
    EXPECT_EQ(game.Loads, 1) << "the announced map was loaded a second time";
}

// A disagreement is reported and nothing else. Whether content mismatch should
// end a session is the identity gate's question, decided at the handshake.
TEST(NetSessionFrame, AClientInADifferentWorldIsNotQuietlyMoved)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    StandaloneHost host;
    ASSERT_TRUE(host.Start("levels/box"));

    Application app(0, nullptr);
    MapAnnouncementClient game(host.Session, host.Session.LocalAddress(),
                               "levels/other");
    ASSERT_EQ(app.Run(game), 0);

    ASSERT_TRUE(game.Connected) << "a content disagreement ended the session";
    EXPECT_EQ(game.Loads, 1)
        << "the client's own world was replaced out from under it";
    EXPECT_EQ(game.CurrentMap, "levels/other");
}

// The recorded gap: an authority that had nothing loaded when this peer joined
// never tells it later. Pinned so that changing it is a decision rather than an
// accident.
TEST(NetSessionFrame, AnAuthorityWithNoWorldLeavesTheClientAsItWas)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    StandaloneHost host;
    ASSERT_TRUE(host.Start(""));

    Application app(0, nullptr);
    MapAnnouncementClient game(host.Session, host.Session.LocalAddress());
    ASSERT_EQ(app.Run(game), 0);

    ASSERT_TRUE(game.Connected);
    EXPECT_EQ(game.Loads, 0);
    EXPECT_TRUE(game.CurrentMap.empty());
}

namespace
{
    // The inverse rig: the engine under test is the authority, and the client
    // is a bare session driven by hand. Nothing here tells the session what map
    // is loaded -- only the frame can, which is what makes this a test of the
    // frame rather than of the setter.
    struct ClientPumpSystem
    {
        NetSession* Client = nullptr;
        Engine* Owner = nullptr;
        int Frames = 0;

        void FrameUpdate(FrameUpdateContext&)
        {
            ++Frames;
            const double now = Frames * (1.0 / 240.0);
            if (Client != nullptr)
            {
                (void)Client->Pump(now);
                Client->Flush(now);
            }
            const bool connected = Client != nullptr && Client->IsConnected();
            if ((connected || Frames > 600) && Owner != nullptr)
                Owner->RequestExit();
        }
    };

    class AnnouncingHostGame final : public Game
    {
    public:
        AnnouncingHostGame(NetSession& client, UdpTransport& clientTransport)
            : ClientSession(client), ClientTransport(clientTransport) {}

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.TargetFps = 2000.0;
        }

        void OnStart(GameStartupContext&) override
        {
            GetEngine().Console().SetMapHandler([](std::string_view) {
                ConsoleResult result;
                result.Status = ConsoleStatus::Ok;
                return result;
            });
            (void)GetEngine().Console().ExecuteTokens(
                { "map", "levels/box" }, ConsoleValueSource{ "test" }, true);

            Session = GetEngine().CreateNetSession(Transport);
            if (Session == nullptr)
                return;
            NetIdentity identity;
            identity.ModuleFingerprint = 7;
            identity.WorldIdentity = 9;
            identity.FixedTickRateMilliHz = 60000;
            Started = Session->Host(0, identity);
            if (Started)
            {
                Dialed = ClientSession.Connect(Session->LocalAddress(), identity);
            }
        }

        void OnRegisterSystems(SystemRegisterContext& ctx) override
        {
            ClientPumpSystem& system = ctx.Schedule.Register<ClientPumpSystem>();
            system.Client = &ClientSession;
            system.Owner = &GetEngine();
        }

        NetSession& ClientSession;
        UdpTransport& ClientTransport;
        UdpTransport Transport;
        NetSession* Session = nullptr;
        bool Started = false;
        bool Dialed = false;
    };
}

// The host half. A session does not read the console -- the frame hands it the
// map, and if it stops doing that every client joins uninformed while every
// test that sets the name by hand keeps passing.
TEST(NetSessionFrame, AHostFrameTellsItsSessionWhichWorldIsLoaded)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    UdpTransport clientTransport;
    NetSession client(clientTransport);

    Application app(0, nullptr);
    AnnouncingHostGame game(client, clientTransport);
    ASSERT_EQ(app.Run(game), 0);

    ASSERT_TRUE(game.Started) << "the host never bound";
    ASSERT_TRUE(game.Dialed) << "the hand-driven client never opened a socket";
    EXPECT_TRUE(client.IsConnected());
    EXPECT_EQ(client.AnnouncedMap(), "levels/box")
        << "the host's frame never told its session what it had loaded, so "
           "every joining client is admitted knowing nothing";
}
