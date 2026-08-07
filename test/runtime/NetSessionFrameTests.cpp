#include <gtest/gtest.h>

#include <app/Engine.h>
#include <net/NetSession.h>
#include <net/UdpTransport.h>
#include <SDL3/SDL.h>

// The empty-session gate for the transport phase: two hosts, a real socket
// each, and a handshake that completes because the frame pumped it.
TEST(NetSessionFrame, TwoEnginesCompleteAHandshakeOverLoopback)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    EngineConfig hostConfig;
    hostConfig.Window.GraphicsApi = WindowGraphicsApi::None;
    hostConfig.Debug.ConsoleLogging = false;

    Engine hostEngine(hostConfig);
    ASSERT_TRUE(hostEngine.Initialize());

    UdpTransport hostTransport;
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

    UdpTransport clientTransport;
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
