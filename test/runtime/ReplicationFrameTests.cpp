#include <gtest/gtest.h>

#include <app/Application.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSession.h>
#include <net/ReplicationRuntime.h>
#include <net/UdpTransport.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <SDL3/SDL.h>

//=============================================================================
// Replication through the real frame
//
// The counterpart to NetSessionFrame.TheFramePhasesPumpTheSession. In each test
// the engine under test never has Publish or Apply called on it by anything
// here, so state can only cross if its own frame phases did the work. The other
// end of the wire is a bare session driven by hand.
//
// One engine per test: a process has one frame loop, and giving a test the
// ability to step a second engine's driver would be a seam that exists only for
// these two tests.
//=============================================================================

namespace
{
    NetIdentity TestIdentity()
    {
        NetIdentity identity;
        identity.ModuleFingerprint = 11;
        identity.WorldIdentity = 22;
        identity.FixedTickRateMilliHz = 60000;
        return identity;
    }

    Transform3f PoseAt(float x, float y, float z)
    {
        Transform3f pose;
        pose.Position = Vec3d{ x, y, z };
        return pose;
    }

    // A world of its own, built from the engine's schema so both ends agree on
    // component identity without sharing anything at runtime.
    struct StandaloneWorld
    {
        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        World Entities;

        StandaloneWorld()
        {
            RegisterEngineRuntimeComponents(Schema);
            Schema.Seal();
            Schema.Apply(Entities);
            RegisterEngineReplicatedComponents(Layout);
            Layout.Seal();
        }
    };

    //-------------------------------------------------------------------------
    // The engine hosts; a hand-driven client reads what its frame published.
    //-------------------------------------------------------------------------
    struct ClientPumpSystem
    {
        NetSession* Client = nullptr;
        StandaloneWorld* Mirror = nullptr;
        ReplicationRuntime* Replication = nullptr;
        Engine* Owner = nullptr;
        int Frames = 0;
        int Applied = 0;
        Vec3d SeenPosition;

        void FrameUpdate(FrameUpdateContext&)
        {
            ++Frames;
            const double now = Frames * (1.0 / 240.0);

            for (const NetSession::Delivery& delivery : Client->Pump(now))
            {
                const SnapshotApplyResult result = Replication->Apply(
                    delivery.Payload, Mirror->Entities, Mirror->Schema, Mirror->Layout);
                if (result.Ok() && result.EntitiesSpawned > 0)
                    ++Applied;
            }
            Client->Flush(now);

            for (const auto& [id, entity] : Replication->ClientEntities().All())
            {
                if (!Mirror->Entities.IsAlive(entity))
                    continue;
                if (const LocalTransform* t =
                        Mirror->Entities.TryGet<LocalTransform>(entity))
                    SeenPosition = t->Value.Position;
            }

            if (Applied > 0 || Frames > 3000)
                Owner->RequestExit();
        }
    };

    class HostingGame final : public Game
    {
    public:
        HostingGame(NetSession& client, StandaloneWorld& mirror,
                    ReplicationRuntime& replication)
            : Client(client), Mirror(mirror), Replication(replication) {}

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.TargetFps = 2000.0;
        }

        void OnStart(GameStartupContext&) override
        {
            NetSession* session = GetEngine().CreateNetSession(Transport);
            if (session == nullptr || !session->Host(0, TestIdentity()))
                return;
            Address = session->LocalAddress();

            World& world = GetEngine().World().Entities();
            const EntityId pawn = world.CreateEntity();
            world.AddComponent<NetReplicated>(pawn);
            world.AddComponent<LocalTransform>(
                pawn, LocalTransform{ PoseAt(12.0f, 34.0f, 56.0f) });
            Hosting = true;
        }

        void OnRegisterSystems(SystemRegisterContext& ctx) override
        {
            // Dialed here rather than in OnStart so the address is known and
            // the very first pumped frame already has the hello in flight.
            Dialed = Client.Connect(Address, TestIdentity());

            ClientPumpSystem& system = ctx.Schedule.Register<ClientPumpSystem>();
            system.Client = &Client;
            system.Mirror = &Mirror;
            system.Replication = &Replication;
            system.Owner = &GetEngine();
            Driver = &system;
        }

        void OnShutdown(GameShutdownContext&) override
        {
            // Copied out while the schedule still owns the system. Application
            // destroys the engine before Run returns, so a pointer into a
            // system is dangling by the time a test body could read it -- the
            // values happen to survive in freed memory, which is why this only
            // shows up under a sanitizer.
            if (Driver != nullptr)
            {
                Applied = Driver->Applied;
                SeenPosition = Driver->SeenPosition;
            }
        }

        UdpTransport Transport;
        NetSession& Client;
        StandaloneWorld& Mirror;
        ReplicationRuntime& Replication;
        NetAddress Address;
        ClientPumpSystem* Driver = nullptr;
        bool Hosting = false;
        bool Dialed = false;
        int Applied = 0;
        Vec3d SeenPosition;
    };

    //-------------------------------------------------------------------------
    // The engine joins; a hand-driven authority publishes into its frame.
    //-------------------------------------------------------------------------
    struct AuthorityPumpSystem
    {
        NetSession* Authority = nullptr;
        StandaloneWorld* Source = nullptr;
        ReplicationRuntime* Replication = nullptr;
        Engine* Owner = nullptr;
        int Frames = 0;

        void FrameUpdate(FrameUpdateContext&)
        {
            ++Frames;
            const double now = Frames * (1.0 / 240.0);

            (void)Authority->Pump(now);
            (void)Replication->Publish(*Authority, Source->Entities, Source->Layout,
                                       static_cast<std::uint64_t>(Frames));
            Authority->Flush(now);

            const bool mirrored =
                Owner->Replication().ClientEntities().Size() > 0;
            if (mirrored || Frames > 3000)
                Owner->RequestExit();
        }
    };

    class JoiningGame final : public Game
    {
    public:
        JoiningGame(NetSession& authority, StandaloneWorld& source,
                    ReplicationRuntime& replication, const NetAddress& address)
            : Authority(authority), Source(source), Replication(replication),
              Address(address) {}

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.TargetFps = 2000.0;
        }

        void OnStart(GameStartupContext&) override
        {
            NetSession* session = GetEngine().CreateNetSession(Transport);
            if (session == nullptr)
                return;
            Joined = session->Connect(Address, TestIdentity());
        }

        void OnRegisterSystems(SystemRegisterContext& ctx) override
        {
            AuthorityPumpSystem& system = ctx.Schedule.Register<AuthorityPumpSystem>();
            system.Authority = &Authority;
            system.Source = &Source;
            system.Replication = &Replication;
            system.Owner = &GetEngine();
        }

        void OnShutdown(GameShutdownContext&) override
        {
            // Read while the world is still alive.
            World& world = GetEngine().World().Entities();
            Mirrored = static_cast<int>(
                GetEngine().Replication().ClientEntities().Size());
            for (const auto& [id, entity] :
                 GetEngine().Replication().ClientEntities().All())
            {
                if (!world.IsAlive(entity))
                    continue;
                if (const LocalTransform* t = world.TryGet<LocalTransform>(entity))
                    SeenPosition = t->Value.Position;
            }
        }

        UdpTransport Transport;
        NetSession& Authority;
        StandaloneWorld& Source;
        ReplicationRuntime& Replication;
        NetAddress Address;
        bool Joined = false;
        int Mirrored = 0;
        Vec3d SeenPosition;
    };
}

// Nothing here calls Publish on the engine, so a snapshot can only reach the
// hand-driven client if the engine's own FlushNet phase produced one.
TEST(ReplicationFrame, TheFlushPhasePublishesWhatTheAuthorityHolds)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    UdpTransport clientTransport;
    NetSession client(clientTransport);
    StandaloneWorld mirror;
    ReplicationRuntime replication;

    Application app(0, nullptr);
    HostingGame game(client, mirror, replication);
    ASSERT_EQ(app.Run(game), 0);

    ASSERT_TRUE(game.Hosting) << "the engine never started hosting";
    ASSERT_TRUE(game.Dialed) << "the hand-driven client never opened a socket";
    ASSERT_NE(game.Driver, nullptr) << "the pump system was never registered";

    EXPECT_GT(game.Applied, 0)
        << "the flush phase never published a snapshot";
    EXPECT_FLOAT_EQ(game.SeenPosition.X, 12.0f);
    EXPECT_FLOAT_EQ(game.SeenPosition.Y, 34.0f);
    EXPECT_FLOAT_EQ(game.SeenPosition.Z, 56.0f);
}

// The mirror image: nothing here calls Apply on the engine, so the entity can
// only appear in its world if its own PumpNet phase applied the snapshot.
TEST(ReplicationFrame, ThePumpPhaseAppliesWhatTheAuthoritySends)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    UdpTransport authorityTransport;
    NetSession authority(authorityTransport);
    ASSERT_TRUE(authority.Host(0, TestIdentity()));

    StandaloneWorld source;
    const EntityId pawn = source.Entities.CreateEntity();
    source.Entities.AddComponent<NetReplicated>(pawn);
    source.Entities.AddComponent<LocalTransform>(
        pawn, LocalTransform{ PoseAt(-7.5f, 2.25f, 100.0f) });

    ReplicationRuntime replication;

    Application app(0, nullptr);
    JoiningGame game(authority, source, replication, authority.LocalAddress());
    ASSERT_EQ(app.Run(game), 0);

    ASSERT_TRUE(game.Joined) << "the engine never dialed the authority";
    EXPECT_EQ(game.Mirrored, 1) << "the pump phase never applied a snapshot";
    EXPECT_FLOAT_EQ(game.SeenPosition.X, -7.5f);
    EXPECT_FLOAT_EQ(game.SeenPosition.Y, 2.25f);
    EXPECT_FLOAT_EQ(game.SeenPosition.Z, 100.0f);
}
