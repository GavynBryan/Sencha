#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <net/ClientPrediction.h>
#include <net/NetMessageRouter.h>
#include <net/NetOwnership.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnRecipe.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationSnapshot.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <array>
#include <cstring>
#include <vector>

//=============================================================================
// A possessable networked object, end to end
//
// The proving feature for the contributor-facing seams. Deliberately not a
// vehicle: no rendering, no physics, no gameplay, so a failure here is a
// networking failure and not something else wearing one.
//
// A turret sits in the world owned by nobody. A client asks to take it, by
// name, in a validated request. The authority grants it. Control moves, and
// everything that follows from control moves with it. The client gives it
// back. Everything comes off. A peer joining afterwards sees where it actually
// ended up.
//
// Every step below goes through an engine seam. None of it reaches a socket, a
// packet header, an ack window, a sequence number, or a fragment -- and none of
// it writes the same fact down twice.
//=============================================================================

namespace
{
    // A small integer and an owner-only float, so the turret also exercises the
    // narrow-scalar path and owner-gated visibility rather than only ownership.
    enum class TurretState : std::uint8_t
    {
        Idle,
        Tracking,
    };

    struct TurretHeat
    {
        float Charge = 0.0f;
        TurretState State = TurretState::Idle;
    };
}

template <>
struct TypeSchema<TurretHeat>
{
    static constexpr std::string_view Name = "test.TurretHeat";
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        return std::tuple{
            // Only whoever is driving it needs to know how hot it is.
            MakeField("charge", &TurretHeat::Charge).OwnerOnly(),
            MakeField("state", &TurretHeat::State),
        };
    }
};

SENCHA_DECLARE_COMPONENT_TYPE(TurretHeat, "test.turret_heat");

namespace
{
    constexpr std::uint8_t kTakeTurret = kNetFirstGamePayloadKind;
    constexpr NetSpawnRecipeId kTurretRecipe = 21;

    // What a client sends to ask for a specific turret. The only field is the
    // object it names, which is the whole point: a request that can name an
    // object is the thing gameplay could not previously write.
    struct TakeTurretRequest
    {
        NetEntityId Target;
    };

    std::vector<std::byte> EncodeTake(NetEntityId target)
    {
        std::vector<std::byte> out(sizeof(std::uint64_t));
        const std::uint64_t value = target.Value;
        std::memcpy(out.data(), &value, sizeof(value));
        return out;
    }

    bool DecodeTake(std::span<const std::byte> body, TakeTurretRequest& out)
    {
        if (body.size() != sizeof(std::uint64_t))
            return false;
        std::uint64_t value = 0;
        std::memcpy(&value, body.data(), sizeof(value));
        out.Target = NetEntityId{ value };
        return out.Target.IsValid();
    }

    // Two worlds and the replication between them, driven by hand.
    struct Session
    {
        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        World Authority;
        World Client;

        ReplicationAuthorityIdentity Identity;
        ReplicationChangeStore Changes;
        ReplicationClientIdentity ClientIdentity;
        ReplicationPeerState Peer;
        NetSnapshotAck ClientAck;
        ClientPrediction Prediction;
        NetSpawnRecipes Recipes;

        std::vector<std::byte> Scratch;
        std::uint64_t Generation = 0;
        std::uint64_t Tick = 0;
        SnapshotApplyResult LastApply;

        // The peer this session's client is.
        static constexpr std::uint32_t kSelf = 2;

        Session() : Scratch(2048)
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            components.Add<TurretHeat>();
            Schema.Seal();
            Schema.Apply(Authority);
            Schema.Apply(Client);
            Layout.Seal();
            EXPECT_EQ(Layout.Error(), ReplicationLayoutError::None)
                << Layout.ErrorDetail();
            Prediction.Bind(Layout);

            EXPECT_TRUE(Recipes.Register(
                kTurretRecipe, [](World& world, EntityId entity) {
                    // What a turret is on the receiving machine beyond its
                    // replicated state.
                    if (!world.HasComponent<LookOrientation>(entity))
                        world.AddComponent<LookOrientation>(entity, LookOrientation{});
                }));
        }

        void Replicate()
        {
            ++Tick;
            Changes.Update(Authority, Layout, Identity, ++Generation);

            SnapshotWriteRequest write;
            write.Changes = &Changes;
            write.Layout = &Layout;
            write.Peer = &Peer;
            write.OwnerPeer = kSelf;
            write.Tick = Tick;
            write.Sequence = Peer.NextSnapshotSequence();
            const SnapshotWriteResult written =
                ReplicationWriteSnapshot(write, Scratch);
            ASSERT_TRUE(written.Ok);

            SnapshotApplyRequest apply;
            apply.Target = &Client;
            apply.Schema = &Schema;
            apply.Layout = &Layout;
            apply.Identity = &ClientIdentity;
            apply.Recipes = &Recipes;
            apply.Prediction = &Prediction;
            LastApply = ReplicationApplySnapshot(
                apply, std::span(Scratch).subspan(0, written.BytesWritten));
            ASSERT_TRUE(LastApply.Ok())
                << SnapshotApplyErrorToString(LastApply.Error);

            if (LastApply.Complete())
            {
                ClientAck.Observe(LastApply.Sequence);
                Peer.Acknowledge(ClientAck);
            }

            // What the engine does in the pump, once snapshots have landed.
            NetReconcileLocalControl(Client, PeerId{ kSelf }, Prediction);
        }

        EntityId Mirror(EntityId authorityEntity) const
        {
            const NetEntityId id = Identity.TryFind(authorityEntity);
            return id.IsValid() ? ClientIdentity.TryResolve(id) : EntityId{};
        }
    };

    // The authority's side of the request. Everything it refuses, it refuses
    // from its own records.
    struct TurretGrantDesk
    {
        Session* Link = nullptr;
        int Refusals = 0;
        int Grants = 0;

        static bool Handle(void* context, const NetMessageContext& message)
        {
            auto& self = *static_cast<TurretGrantDesk*>(context);
            Session& session = *self.Link;

            TakeTurretRequest request;
            if (!DecodeTake(message.Body, request))
            {
                ++self.Refusals;
                return false;
            }

            // The identity resolves through the map the authority minted, and
            // never through anything the message says about which entity that
            // is. An id this authority never handed out names nothing.
            const EntityId target = session.Identity.TryResolve(request.Target);
            if (!target.IsValid() || !message.Entities.IsAlive(target))
            {
                ++self.Refusals;
                return false;
            }

            // Game policy, asked of the world: a turret somebody else is
            // already driving is not available.
            if (!message.Entities.HasComponent<TurretHeat>(target))
            {
                ++self.Refusals;
                return false;
            }
            if (NetOwnerOf(message.Entities, target).IsValid())
            {
                ++self.Refusals;
                return false;
            }

            NetSetOwner(message.Entities, target, message.From);
            ++self.Grants;
            return true;
        }
    };

    EntityId SpawnTurret(World& world, float x)
    {
        Transform3f pose;
        pose.Position = Vec3d{ x, 0.0f, 0.0f };
        const EntityId turret = world.CreateEntity();
        world.AddComponent<NetReplicated>(turret);
        world.AddComponent<LocalTransform>(turret, LocalTransform{ pose });
        world.AddComponent<TurretHeat>(turret, TurretHeat{});
        world.AddComponent<NetSpawnRecipe>(turret,
                                           NetSpawnRecipe{ .Id = kTurretRecipe });
        return turret;
    }
}

TEST(PossessionProof, AClientTakesATurretAndGivesItBack)
{
    Session session;
    NetMessageRouter router;
    TurretGrantDesk desk{ &session };
    ASSERT_TRUE(router.Bind(kTakeTurret, NetMessageDirection::ClientToAuthority,
                            &TurretGrantDesk::Handle, &desk));

    // 1. The authority has a turret. Nobody owns it.
    const EntityId turret = SpawnTurret(session.Authority, 5.0f);
    session.Replicate();

    const EntityId mirror = session.Mirror(turret);
    ASSERT_TRUE(mirror.IsValid()) << "the turret never reached the client";
    EXPECT_TRUE(session.Client.HasComponent<LookOrientation>(mirror))
        << "the recipe did not complete it";
    EXPECT_FALSE(LocalControlSubjectOf(session.Client).IsValid());

    // 2. The client names that turret, by the name replication gave it.
    const NetEntityId named = session.ClientIdentity.TryFind(mirror);
    ASSERT_TRUE(named.IsValid())
        << "a client cannot say which object it means";

    const std::vector<std::byte> body = EncodeTake(named);
    const NetMessageContext request{
        .From = PeerId{ Session::kSelf },
        .Entities = session.Authority,
        .Objects = nullptr,
        .Body = body,
    };

    // 3. The authority grants it.
    ASSERT_TRUE(router.Route(NetSessionRole::Host, kTakeTurret, request));
    EXPECT_EQ(desk.Grants, 1);

    // 4 and 5. Input routing and local control follow, without the game
    // touching either.
    EXPECT_EQ(NetOwnerOf(session.Authority, turret), PeerId{ Session::kSelf });
    const InputActionSourceRef* steering =
        session.Authority.TryGet<InputActionSourceRef>(turret);
    ASSERT_NE(steering, nullptr) << "whose keys drive it was never installed";
    EXPECT_EQ(steering->Source, Session::kSelf);

    session.Replicate();
    EXPECT_EQ(LocalControlSubjectOf(session.Client), mirror);
    EXPECT_TRUE(session.Client.HasComponent<LocalLookControl>(mirror));
    EXPECT_EQ(session.Prediction.Predicted(), mirror);

    // 6. Owner-only state reaches the owner and nothing else does.
    if (TurretHeat* heat = session.Authority.TryGet<TurretHeat>(turret))
    {
        heat->Charge = 0.75f;
        heat->State = TurretState::Tracking;
    }
    session.Replicate();

    const TurretHeat* seen = session.Client.TryGet<TurretHeat>(mirror);
    ASSERT_NE(seen, nullptr);
    EXPECT_EQ(seen->State, TurretState::Tracking)
        << "a narrow field did not survive the round trip";
    EXPECT_FLOAT_EQ(seen->Charge, 0.75f)
        << "the owner was not sent the state only an owner may see";

    // 7 and 8. The player gives it back, and everything comes off.
    NetClearOwner(session.Authority, turret);
    session.Replicate();

    EXPECT_FALSE(NetOwnerOf(session.Authority, turret).IsValid());
    EXPECT_EQ(session.Authority.TryGet<InputActionSourceRef>(turret), nullptr);
    EXPECT_FALSE(LocalControlSubjectOf(session.Client).IsValid())
        << "the client still drives a turret it handed back";
    EXPECT_FALSE(session.Client.HasComponent<LocalLookControl>(mirror));
    EXPECT_FALSE(session.Prediction.Predicted().IsValid())
        << "the client still predicts a turret it handed back";
}

// The transfer the previous architecture could not express at all: A to B with
// nobody relinquishing first, and no frame in which both drive it.
TEST(PossessionProof, ATurretMovesBetweenPeersWithoutEitherLettingGoFirst)
{
    Session session;
    const EntityId turret = SpawnTurret(session.Authority, 1.0f);
    NetSetOwner(session.Authority, turret, PeerId{ 2 });
    ASSERT_EQ(session.Authority.TryGet<InputActionSourceRef>(turret)->Source, 2u);

    NetSetOwner(session.Authority, turret, PeerId{ 3 });

    EXPECT_EQ(NetOwnerOf(session.Authority, turret), PeerId{ 3 });
    EXPECT_EQ(session.Authority.TryGet<InputActionSourceRef>(turret)->Source, 3u)
        << "one peer's aim turns a turret another peer's keys still move";

    std::vector<EntityId> owned;
    NetOwnedBy(session.Authority, PeerId{ 2 }, owned);
    EXPECT_TRUE(owned.empty());
}

//-----------------------------------------------------------------------------
// What the authority refuses, and why each refusal is its own to make
//-----------------------------------------------------------------------------

TEST(PossessionProof, ARequestNamingNothingIsRefused)
{
    Session session;
    NetMessageRouter router;
    TurretGrantDesk desk{ &session };
    ASSERT_TRUE(router.Bind(kTakeTurret, NetMessageDirection::ClientToAuthority,
                            &TurretGrantDesk::Handle, &desk));

    const std::vector<std::byte> body = EncodeTake(NetEntityId{ 4242 });
    const NetMessageContext request{
        .From = PeerId{ Session::kSelf },
        .Entities = session.Authority,
        .Objects = nullptr,
        .Body = body,
    };

    EXPECT_FALSE(router.Route(NetSessionRole::Host, kTakeTurret, request));
    EXPECT_EQ(desk.Refusals, 1);
    EXPECT_EQ(desk.Grants, 0);
}

TEST(PossessionProof, ATurretSomebodyElseDrivesIsRefused)
{
    Session session;
    NetMessageRouter router;
    TurretGrantDesk desk{ &session };
    ASSERT_TRUE(router.Bind(kTakeTurret, NetMessageDirection::ClientToAuthority,
                            &TurretGrantDesk::Handle, &desk));

    const EntityId turret = SpawnTurret(session.Authority, 2.0f);
    session.Replicate();
    NetSetOwner(session.Authority, turret, PeerId{ 9 });

    const std::vector<std::byte> body =
        EncodeTake(session.Identity.TryFind(turret));
    const NetMessageContext request{
        .From = PeerId{ Session::kSelf },
        .Entities = session.Authority,
        .Objects = nullptr,
        .Body = body,
    };

    EXPECT_FALSE(router.Route(NetSessionRole::Host, kTakeTurret, request));
    EXPECT_EQ(desk.Grants, 0);
    EXPECT_EQ(NetOwnerOf(session.Authority, turret), PeerId{ 9 })
        << "a request took a turret out from under the peer driving it";
}

// 9. A peer that joins after all of it sees where things actually ended up,
// rather than the sequence that got there.
TEST(PossessionProof, APeerJoiningLaterSeesTheTurretAsItIsNow)
{
    Session session;
    const EntityId turret = SpawnTurret(session.Authority, 3.0f);
    session.Replicate();
    NetSetOwner(session.Authority, turret, PeerId{ Session::kSelf });
    session.Replicate();
    NetClearOwner(session.Authority, turret);
    session.Replicate();

    // A second client, hearing about this world for the first time.
    World latecomer;
    session.Schema.Apply(latecomer);
    ReplicationClientIdentity latecomerIdentity;
    ReplicationPeerState fresh;

    ++session.Tick;
    session.Changes.Update(session.Authority, session.Layout, session.Identity,
                           ++session.Generation);
    SnapshotWriteRequest write;
    write.Changes = &session.Changes;
    write.Layout = &session.Layout;
    write.Peer = &fresh;
    write.OwnerPeer = 7;
    write.Tick = session.Tick;
    write.Sequence = fresh.NextSnapshotSequence();
    const SnapshotWriteResult written =
        ReplicationWriteSnapshot(write, session.Scratch);
    ASSERT_TRUE(written.Ok);

    SnapshotApplyRequest apply;
    apply.Target = &latecomer;
    apply.Schema = &session.Schema;
    apply.Layout = &session.Layout;
    apply.Identity = &latecomerIdentity;
    apply.Recipes = &session.Recipes;
    const SnapshotApplyResult applied = ReplicationApplySnapshot(
        apply, std::span(session.Scratch).subspan(0, written.BytesWritten));
    ASSERT_TRUE(applied.Ok()) << SnapshotApplyErrorToString(applied.Error);

    const EntityId seeded =
        latecomerIdentity.TryResolve(session.Identity.TryFind(turret));
    ASSERT_TRUE(seeded.IsValid());

    const NetOwner* owner = latecomer.TryGet<NetOwner>(seeded);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(owner->Peer, kNetAuthorityPeer)
        << "a peer joining after the handback was told somebody still drives it";
    EXPECT_EQ(latecomer.TryGet<TurretHeat>(seeded)->Charge, 0.0f)
        << "owner-only state reached a peer that does not own it";
}
