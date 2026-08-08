#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <net/NetPlayerCommand.h>
#include <net/NetReplicationComponents.h>
#include <net/PeerCommandRuntime.h>

#include <array>
#include <cstddef>

//=============================================================================
// A peer's request becoming something the simulation reads.
//
// This is the handoff the whole input channel exists for: past it, the
// authority's own systems steer a remote player's pawn with the same code that
// steers the local one, differing only in which source they read. What is
// asserted here is that the request lands in that peer's source and on that
// peer's pawn -- and nowhere else.
//=============================================================================

namespace
{
    constexpr InputActionId kMove{ 1 };
    constexpr InputActionId kJump{ 2 };
    constexpr std::size_t kActionCount = 2;
    constexpr PeerId kAlice{ 1 };
    constexpr PeerId kBob{ 2 };

    World MakeWorld()
    {
        World world;
        world.RegisterComponent<InputActionSourceRef>();
        world.RegisterComponent<LookOrientation>();
        world.RegisterComponent<NetOwner>();
        InputActionState& local = world.AddResource<InputActionState>();
        local.Configure(kActionCount);
        return world;
    }

    EntityId SpawnPawnFor(World& world, PeerId peer)
    {
        const EntityId pawn = world.CreateEntity();
        world.AddComponent<NetOwner>(pawn, NetOwner{ .Peer = peer.Value });
        world.AddComponent<LookOrientation>(pawn, LookOrientation{});
        world.AddComponent<InputActionSourceRef>(
            pawn, InputActionSourceRef{ .Source = peer.Value });
        return pawn;
    }

    // One tick of a player asking to run forward, optionally jumping, looking
    // in a given direction.
    NetPlayerCommand Asking(std::uint64_t tick, float forward, bool jump,
                            float yaw = 0.0f, float pitch = 0.0f)
    {
        NetPlayerCommand command;
        command.RecordCount = 1;

        NetCommandRecord& record = command.Records[0];
        record.Tick = tick;
        record.Yaw = yaw;
        record.Pitch = pitch;
        record.ActionCount = kActionCount;
        record.Actions[InputActionRegistry::IndexOf(kMove)].Y = forward;
        if (jump)
        {
            record.Actions[InputActionRegistry::IndexOf(kJump)].Flags =
                InputActionFlags::Held | InputActionFlags::Fired;
        }
        return command;
    }

    // What the wire actually carries, kind byte and all -- the same bytes
    // PumpNet hands over, so nothing here tests a shortcut the session does not
    // take.
    struct Datagram
    {
        std::array<std::byte, 256> Bytes{};
        std::size_t Size = 0;

        explicit Datagram(const NetPlayerCommand& command)
        {
            Bytes[0] = static_cast<std::byte>(NetPayloadKind::Command);
            NetBitWriter writer(std::span<std::byte>(Bytes).subspan(1));
            const std::size_t records = NetEncodePlayerCommand(command, writer);
            Size = records == 0 ? 0 : 1 + writer.BytesWritten();
        }

        [[nodiscard]] std::span<const std::byte> View() const
        {
            return std::span<const std::byte>(Bytes).subspan(0, Size);
        }
    };

    bool Deliver(PeerCommandRuntime& commands, PeerId peer,
                 const NetPlayerCommand& command)
    {
        const Datagram datagram(command);
        return commands.Receive(peer, datagram.View());
    }
}

TEST(PeerCommandFeed, PutsAPeersActionsInThatPeersSource)
{
    World world = MakeWorld();
    const EntityId pawn = SpawnPawnFor(world, kAlice);

    PeerCommandRuntime commands;
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(10, 1.0f, true)));
    commands.Feed(world, 100);

    const InputActionSources sources(world);
    EXPECT_FLOAT_EQ(sources.TickFor(pawn).Axis2(kMove).Y, 1.0f);
    EXPECT_TRUE(sources.TickFor(pawn).Fired(kJump))
        << "a request no action column carries is a request that cannot reach "
           "the authority at all";
}

// The gate this slice exists for, stated as a mechanism: everything the local
// player can ask for, a remote player can ask for through the same columns, and
// the authority reads both the same way.
TEST(PeerCommandFeed, ARemotePlayerCanAskForWhatTheLocalPlayerCan)
{
    World world = MakeWorld();
    const EntityId theirs = SpawnPawnFor(world, kAlice);
    const EntityId mine = world.CreateEntity();

    // The local player holds forward and does not jump.
    InputActionState& local = world.GetResource<InputActionState>();
    const std::span<InputActionValue> values = local.BeginTick(100);
    for (InputActionValue& value : values)
        value = InputActionValue{};
    values[InputActionRegistry::IndexOf(kMove)].Y = 1.0f;

    // The remote player jumps and stands still.
    PeerCommandRuntime commands;
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(10, 0.0f, true)));
    commands.Feed(world, 100);

    const InputActionSources sources(world);
    EXPECT_TRUE(sources.TickFor(theirs).Fired(kJump));
    EXPECT_FLOAT_EQ(sources.TickFor(theirs).Axis2(kMove).Y, 0.0f);

    EXPECT_FALSE(sources.TickFor(mine).Fired(kJump))
        << "a remote jump reached the local player";
    EXPECT_FLOAT_EQ(sources.TickFor(mine).Axis2(kMove).Y, 1.0f)
        << "a remote command overwrote the local player's own input";
}

TEST(PeerCommandFeed, OnePeersCommandNeverReachesAnothersPawn)
{
    World world = MakeWorld();
    const EntityId alice = SpawnPawnFor(world, kAlice);
    const EntityId bob = SpawnPawnFor(world, kBob);

    PeerCommandRuntime commands;
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(1, 1.0f, true, 2.0f)));
    commands.Feed(world, 1);

    const InputActionSources sources(world);
    EXPECT_FLOAT_EQ(sources.TickFor(alice).Axis2(kMove).Y, 1.0f);
    EXPECT_FLOAT_EQ(sources.TickFor(bob).Axis2(kMove).Y, 0.0f);
    EXPECT_FLOAT_EQ(world.TryGet<LookOrientation>(alice)->Yaw, 2.0f);
    EXPECT_FLOAT_EQ(world.TryGet<LookOrientation>(bob)->Yaw, 0.0f)
        << "whose view a command turns is the authority's record, not the "
           "message's claim";
}

TEST(PeerCommandFeed, AimLandsOnTheOwnedPawnAndIsClampedToItsLimits)
{
    World world = MakeWorld();
    const EntityId pawn = SpawnPawnFor(world, kAlice);
    LookOrientation* look = world.TryGet<LookOrientation>(pawn);
    look->MinPitch = -0.5f;
    look->MaxPitch = 0.5f;

    PeerCommandRuntime commands;
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(1, 0.0f, false, 1.5f, 3.0f)));
    commands.Feed(world, 1);

    EXPECT_FLOAT_EQ(world.TryGet<LookOrientation>(pawn)->Yaw, 1.5f);
    EXPECT_FLOAT_EQ(world.TryGet<LookOrientation>(pawn)->Pitch, 0.5f)
        << "how far a thing can look is a property of the thing, not of what a "
           "client asked for";
}

// One record per tick, not one per frame: a frame that runs three ticks owes a
// remote player three ticks of input, or remote players are slower than local
// ones exactly when the machine is struggling.
TEST(PeerCommandFeed, FeedsOneRecordPerTick)
{
    World world = MakeWorld();
    const EntityId pawn = SpawnPawnFor(world, kAlice);

    PeerCommandRuntime commands;
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(1, 0.25f, false)));
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(2, 0.50f, false)));
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(3, 0.75f, false)));

    const float expected[] = { 0.25f, 0.50f, 0.75f };
    for (std::uint64_t tick = 0; tick < 3; ++tick)
    {
        commands.Feed(world, tick);
        const InputActionSources sources(world);
        EXPECT_FLOAT_EQ(sources.TickFor(pawn).Axis2(kMove).Y, expected[tick])
            << "tick " << tick;
    }
}

// A knob nobody can reach is a constant with extra steps. The runtime holds
// the tuned value; the buffers are what actually hold input back, so the value
// has to arrive there before it means anything.
TEST(PeerCommandFeed, TheTunedSlackReachesThePeerBuffers)
{
    World world = MakeWorld();
    (void)SpawnPawnFor(world, kAlice);

    PeerCommandRuntime commands;
    commands.SetTargetDepth(5);
    ASSERT_EQ(commands.TargetDepth(), 5u);

    ASSERT_TRUE(Deliver(commands, kAlice, Asking(1, 1.0f, false)));
    commands.Feed(world, 1);

    const NetPeerCommandBuffer* buffer = commands.Peer(kAlice);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->TargetDepth(), 5u);

    // And it keeps following: tuning mid-session is the whole point of a cvar.
    commands.SetTargetDepth(0);
    commands.Feed(world, 2);
    EXPECT_EQ(commands.Peer(kAlice)->TargetDepth(), 0u);
}

TEST(PeerCommandFeed, RefusesAPayloadThatIsNotACommand)
{
    PeerCommandRuntime commands;

    const std::array<std::byte, 4> snapshot{
        static_cast<std::byte>(NetPayloadKind::Snapshot),
        std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 } };
    EXPECT_FALSE(commands.Receive(kAlice, snapshot));

    const std::array<std::byte, 3> garbage{
        static_cast<std::byte>(NetPayloadKind::Command),
        std::byte{ 0xFF }, std::byte{ 0xFF } };
    EXPECT_FALSE(commands.Receive(kAlice, garbage))
        << "a truncated command must not be accepted as a short one";

    EXPECT_EQ(commands.TrackedPeers(), 0u);
}

// A peer that has not spoken has no input. Feeding zeros would stop a pawn that
// nothing ever started, which reads as a stutter rather than as a peer that has
// not finished joining.
TEST(PeerCommandFeed, LeavesASilentPeersSourceAlone)
{
    World world = MakeWorld();
    const EntityId pawn = SpawnPawnFor(world, kAlice);

    PeerCommandRuntime commands;
    commands.Feed(world, 1);

    const InputActionSources sources(world);
    EXPECT_FLOAT_EQ(sources.TickFor(pawn).Axis2(kMove).Y, 0.0f);
    EXPECT_EQ(world.TryGetResource<InputActionSourceTable>(), nullptr)
        << "no peer has spoken, so there is nothing to hold";
}

TEST(PeerCommandFeed, ADepartedPeerStopsCostingAnything)
{
    PeerCommandRuntime commands;
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(1, 1.0f, false)));
    ASSERT_TRUE(Deliver(commands, kBob, Asking(1, 1.0f, false)));
    EXPECT_EQ(commands.TrackedPeers(), 2u);

    commands.ForgetPeer(kAlice);
    EXPECT_EQ(commands.TrackedPeers(), 1u);
    EXPECT_EQ(commands.Peer(kAlice), nullptr);

    commands.Reset();
    EXPECT_EQ(commands.TrackedPeers(), 0u);
}

// Before a profile binds there is no vocabulary to index a record by, and a
// record read under no vocabulary would be read under whatever happens to be
// in memory.
TEST(PeerCommandFeed, DoesNothingBeforeAnActionVocabularyExists)
{
    World world;
    world.RegisterComponent<InputActionSourceRef>();
    world.RegisterComponent<LookOrientation>();
    world.RegisterComponent<NetOwner>();

    PeerCommandRuntime commands;
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(1, 1.0f, true)));
    commands.Feed(world, 1);

    EXPECT_EQ(world.TryGetResource<InputActionSourceTable>(), nullptr);
}

// The defect this closes: aim used to ride once per datagram and be applied to
// every tick fed from it. A frame that ran several ticks turned the pawn all at
// once at the end instead of through the arc the player actually swept, and the
// client -- which framed each tick against its own live look -- walked a
// different path. On a corner that is the difference between catching it and
// not.
TEST(PeerCommandFeed, EachTickTurnsByTheAimItWasTakenWith)
{
    World world = MakeWorld();
    const EntityId pawn = SpawnPawnFor(world, kAlice);

    PeerCommandRuntime commands;
    commands.SetTargetDepth(0);

    // First contact takes only the newest record, so this is what sets the
    // floor the window behind it is measured against.
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(9, 1.0f, false, 0.0f)));
    commands.Feed(world, 1);

    // Now one datagram carrying two ticks swept through different angles --
    // what a redundancy window looks like after a dropped packet.
    NetPlayerCommand command;
    command.RecordCount = 2;
    command.Records[0] = Asking(11, 1.0f, false, 0.75f).Records[0];
    command.Records[1] = Asking(10, 1.0f, false, 0.25f).Records[0];
    ASSERT_TRUE(Deliver(commands, kAlice, command));

    commands.Feed(world, 2);
    EXPECT_FLOAT_EQ(world.TryGet<LookOrientation>(pawn)->Yaw, 0.25f)
        << "the first tick was turned by an aim belonging to a later one";

    commands.Feed(world, 3);
    EXPECT_FLOAT_EQ(world.TryGet<LookOrientation>(pawn)->Yaw, 0.75f);
}

// A starved tick repeats the last record, so it keeps the last aim: a player
// whose datagram was late is still looking where they were looking.
TEST(PeerCommandFeed, AStarvedTickHoldsTheAimItLastHad)
{
    World world = MakeWorld();
    const EntityId pawn = SpawnPawnFor(world, kAlice);

    PeerCommandRuntime commands;
    commands.SetTargetDepth(0);
    ASSERT_TRUE(Deliver(commands, kAlice, Asking(5, 1.0f, false, 1.25f)));

    commands.Feed(world, 1);
    ASSERT_FLOAT_EQ(world.TryGet<LookOrientation>(pawn)->Yaw, 1.25f);

    world.TryGet<LookOrientation>(pawn)->Yaw = 99.0f;
    commands.Feed(world, 2);  // nothing arrived
    EXPECT_FLOAT_EQ(world.TryGet<LookOrientation>(pawn)->Yaw, 1.25f);
}
