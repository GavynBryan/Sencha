#include <gtest/gtest.h>

#include <input/InputAction.h>
#include <net/LoopbackTransport.h>
#include <net/NetChannel.h>
#include <net/NetPlayerCommand.h>
#include <net/NetProtocol.h>
#include <net/NetSession.h>
#include <net/NetTickEstimator.h>
#include <net/SimulatedTransport.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

//=============================================================================
// A whole session over a connection that is behaving badly.
//
// Every other net test proves one mechanism against inputs handed to it. The
// arithmetic in the tick estimator, the depth policy in the command buffer, and
// the redundancy window in the codec were each pinned that way, and each of them
// was pinned against a schedule its own test invented. What none of them can say
// is what those mechanisms do to each other once a real datagram is late, or
// lost, or arrives behind the one sent after it -- which is the only condition
// any of them exist for.
//
// So this runs the real thing: two sessions, a real handshake, real keepalives,
// real round-trip measurement, and real commands, with an impairment schedule
// between them. Everything is seeded and stepped by hand, so a failure here
// replays from its seed rather than from having been unlucky.
//
// What it is not: replication does not run here, so the shared clock is fed by
// keepalives rather than by the snapshots production feeds it from. That is the
// sparser of the two sample rates, which makes alignment harder to hold rather
// than easier -- these are lower bounds on how well the real thing behaves.
//=============================================================================

namespace
{
    constexpr double kTickSeconds = 1.0 / 60.0;

    // The authority counts from somewhere a client's own counter will not reach
    // on its own. Two machines starting near zero on a loopback make a stamp
    // built from the shared clock and a stamp built from the local tick the same
    // number, and every alignment assertion below would hold for the wrong
    // reason.
    constexpr std::uint64_t kAuthorityEpoch = 4'000'000;

    NetIdentity LinkIdentity()
    {
        NetIdentity identity;
        identity.ModuleFingerprint = 0xA11CE;
        identity.WorldIdentity = 0xB0B;
        identity.FixedTickRateMilliHz = 60000;
        return identity;
    }

    struct Endpoint
    {
        Endpoint(LoopbackNetwork& network, const NetImpairment& impairment)
            : Wire(network), Link(Wire, impairment), Session(Link)
        {
        }

        LoopbackTransport Wire;
        SimulatedTransport Link;
        NetSession Session;
    };

    //-------------------------------------------------------------------------
    // Two machines a fixed tick apart, joined by an impaired link.
    //
    // Pump and Flush are separate because the frame keeps them separate: what a
    // tick decides to send is decided between them, and folding them together
    // would give every command an extra frame of age that production does not
    // pay.
    //-------------------------------------------------------------------------
    class DegradedLink
    {
    public:
        // Slack defaults to what the shipped cvar defaults to. Stated rather
        // than inherited, because every stamp below is this many ticks of
        // margin wide.
        explicit DegradedLink(const NetImpairment& impairment,
                              std::uint32_t slackTicks = static_cast<std::uint32_t>(
                                  NetPeerCommandBuffer::kTargetDepth))
            : Host(Network, impairment), Client(Network, impairment)
        {
            Clock.SetSlackTicks(slackTicks);
        }

        [[nodiscard]] bool Join(int maxSteps)
        {
            if (!Host.Session.Host(0, LinkIdentity()))
                return false;
            if (!Client.Session.Connect(Host.Session.LocalAddress(), LinkIdentity()))
                return false;

            for (int step = 0; step < maxSteps && !Client.Session.IsConnected(); ++step)
                Step();
            return Client.Session.IsConnected();
        }

        void Pump()
        {
            Now += kTickSeconds;
            ++HostTick;
            ++ClientTick;

            Host.Session.SetLocalTick(HostTick);
            Client.Session.SetLocalTick(ClientTick);

            HostIn = Host.Session.Pump(Now);
            ClientIn = Client.Session.Pump(Now);

            if (!Client.Session.IsConnected())
                return;

            // One sample per keepalive that lands. Observing the same number
            // every frame would drag the estimate toward a round trip measured
            // seconds ago, which is the trap the frame phase documents.
            const std::uint64_t heard = Client.Session.AuthorityTick();
            if (heard == 0 || heard == LastHeard)
                return;

            LastHeard = heard;
            PreviousOffset = Clock.HasEstimate() ? Clock.CommandOffset() : 0;
            Clock.Observe(heard, ClientTick, Client.Session.RoundTripMicroseconds(),
                          kTickSeconds);
            ++Observations;
            if (Observations > 1)
            {
                const std::int64_t moved = Clock.CommandOffset() - PreviousOffset;
                OffsetRise = std::max<std::int64_t>(OffsetRise, moved);
                OffsetFall = std::max<std::int64_t>(OffsetFall, -moved);
            }
        }

        void Flush()
        {
            Host.Session.Flush(Now);
            Client.Session.Flush(Now);
        }

        void Step()
        {
            Pump();
            Flush();
        }

        LoopbackNetwork Network;
        Endpoint Host;
        Endpoint Client;

        NetTickEstimator Clock;
        std::vector<NetSession::Delivery> HostIn;
        std::vector<NetSession::Delivery> ClientIn;

        double Now = 0.0;
        std::uint64_t HostTick = kAuthorityEpoch;
        std::uint64_t ClientTick = 0;

        std::uint64_t Observations = 0;
        // The largest the stamp moved between two samples, kept per direction
        // because the two directions cost different things. Moving it back
        // toward the present risks stamping a tick the authority has already
        // simulated, which is input refused outright; moving it further ahead
        // only queues, and the buffer sheds a queue.
        std::int64_t OffsetRise = 0;
        std::int64_t OffsetFall = 0;

    private:
        std::uint64_t LastHeard = 0;
        std::int64_t PreviousOffset = 0;
    };

    //-------------------------------------------------------------------------
    // The client's outgoing command, built the way the flush phase builds it.
    //
    // Not through PeerCommandRuntime::SendLocal, which needs a World and a
    // resolved action vocabulary to read from: what this link is testing is what
    // becomes of the datagram, not how its records were filled.
    //-------------------------------------------------------------------------
    constexpr std::uint8_t kActionCount = 2;
    constexpr std::size_t kMoveAction = 0;
    constexpr std::size_t kTapAction = 1;

    // Sends one command carrying the last kNetMaxCommandRecords ticks, newest
    // first. `tapTick` is a local tick the player pressed something on; it rides
    // the window like any other input and should be simulated once.
    void SendCommand(DegradedLink& link, std::uint64_t tapTick)
    {
        if (!link.Clock.HasEstimate() || link.ClientTick < kNetMaxCommandRecords)
            return;

        NetPlayerCommand command;
        command.Yaw = 0.5f;
        command.Pitch = 0.25f;
        command.RecordCount = static_cast<std::uint8_t>(kNetMaxCommandRecords);

        for (std::size_t index = 0; index < kNetMaxCommandRecords; ++index)
        {
            const std::uint64_t localTick = link.ClientTick - index;
            NetCommandRecord& record = command.Records[index];

            // Renamed onto the authority's clock, keeping the window a run of
            // consecutive ticks: a gap in it reads as ticks the authority must
            // fill for itself.
            record.Tick = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(localTick) + link.Clock.CommandOffset());
            record.ActionCount = kActionCount;

            // Held the whole run, which is what a player walking forward looks
            // like and what the starvation policy has to preserve.
            record.Actions[kMoveAction].X = 1.0f;
            record.Actions[kMoveAction].Flags = InputActionFlags::Held;

            record.Actions[kTapAction] = InputActionValue{};
            if (localTick == tapTick)
            {
                record.Actions[kTapAction].Flags =
                    InputActionFlags::Pressed | InputActionFlags::Fired;
            }
        }

        std::array<std::byte, kNetMaxPayloadBytes> scratch{};
        scratch[0] = static_cast<std::byte>(NetPayloadKind::Command);
        NetBitWriter writer(std::span<std::byte>(scratch).subspan(1));
        if (NetEncodePlayerCommand(command, writer) == 0)
            return;

        (void)link.Client.Session.Send(
            link.Client.Session.LocalPeerId(), NetChannelKind::UnreliableSequenced,
            std::span<const std::byte>(scratch).subspan(0, 1 + writer.BytesWritten()));
    }

    struct InputSoak
    {
        std::uint64_t Simulated = 0;
        std::uint64_t Starved = 0;
        std::uint64_t BackedUp = 0;
        std::uint64_t Ticks = 0;
        std::uint64_t QueuedTotal = 0;
        std::size_t Deepest = 0;
        std::uint64_t HeldDropped = 0;
        // What the link did to the client's traffic, so a run cannot pass by
        // having been a clean one.
        std::uint64_t Dropped = 0;
        std::uint64_t Delayed = 0;

        [[nodiscard]] double StarvedRate() const
        {
            return Simulated == 0
                ? 1.0
                : static_cast<double>(Starved) / static_cast<double>(Simulated);
        }
        [[nodiscard]] double BackedUpRate() const
        {
            return Simulated == 0
                ? 1.0
                : static_cast<double>(BackedUp) / static_cast<double>(Simulated);
        }
        // Records held on an average tick. This is the latency side of the
        // trade in the unit it is actually paid in: every record waiting is a
        // tick between the player pressing something and the authority running
        // it. Depth-above-target cannot say this, because raising the target
        // moves the line being measured against.
        [[nodiscard]] double MeanDepth() const
        {
            return Ticks == 0
                ? 0.0
                : static_cast<double>(QueuedTotal) / static_cast<double>(Ticks);
        }
    };

    // The authority's side of the same frame: everything that arrived goes into
    // the peer's buffer. This is what PeerCommandRuntime::Receive does, minus the
    // per-peer map and the world write neither of which this link exercises.
    void ReceiveCommands(const DegradedLink& link, NetPeerCommandBuffer& buffer)
    {
        for (const NetSession::Delivery& delivery : link.HostIn)
        {
            if (delivery.Payload.empty())
                continue;
            if (static_cast<NetPayloadKind>(delivery.Payload[0])
                != NetPayloadKind::Command)
            {
                continue;
            }

            NetBitReader reader(
                std::span<const std::byte>(delivery.Payload).subspan(1));
            NetPlayerCommand command;
            if (NetDecodePlayerCommand(reader, command))
                buffer.Receive(command);
        }
    }

    // A whole session's worth of input: the client sending one command a frame,
    // the authority consuming one record a tick, for as long as it takes to say
    // something about the steady state.
    [[nodiscard]] InputSoak RunInputSoak(const NetImpairment& impairment,
                                         std::uint32_t slackTicks, int steps)
    {
        DegradedLink link(impairment, slackTicks);
        InputSoak soak;
        if (!link.Join(600))
            return soak;

        // One cvar drives both ends of the margin, so the soak sets both: the
        // client stamps this far ahead so its records land above the admission
        // floor, and the authority holds this deep so a late one still has a
        // tick's worth of cushion in front of it.
        NetPeerCommandBuffer buffer;
        buffer.SetTargetDepth(slackTicks);

        // Depth above the cushion the target asks for is the collapse policy's
        // business, and is allowed to be there briefly -- a round trip that grew
        // stamps the next few commands further ahead, and they queue until the
        // collapse sheds them. What must not happen is living there.
        const std::size_t tolerated = buffer.TargetDepth() + 1;

        for (int step = 0; step < steps; ++step)
        {
            link.Pump();
            ReceiveCommands(link, buffer);

            // Exactly one record per tick, which is the contract that makes
            // depth mean latency.
            NetCommandRecord record;
            if (buffer.Next(record))
            {
                ++soak.Simulated;
                if (!record.Actions[kMoveAction].IsHeld())
                    ++soak.HeldDropped;
            }

            soak.Deepest = std::max(soak.Deepest, buffer.QueuedTicks());
            soak.QueuedTotal += buffer.QueuedTicks();
            ++soak.Ticks;
            if (buffer.QueuedTicks() > tolerated)
                ++soak.BackedUp;

            SendCommand(link, /*tapTick*/ 0);
            link.Flush();
        }

        soak.Starved = buffer.StarvedTicks();
        soak.Dropped = link.Client.Link.Dropped();
        soak.Delayed = link.Client.Link.Delayed();
        return soak;
    }

    // Loss, delay, jitter, and reordering at once -- the shape of a connection
    // that is bad but not broken.
    [[nodiscard]] NetImpairment BadConnection()
    {
        NetImpairment impairment;
        impairment.LossPercent = 10;
        impairment.ReorderPercent = 3;
        impairment.LatencySteps = 4;
        impairment.JitterSteps = 2;
        impairment.Seed = 4242;
        return impairment;
    }
}

//=============================================================================
// The connection itself
//=============================================================================

// A fifth of everything lost, for long enough that the ten-second timeout would
// have fired several times over if keepalives were not surviving it.
TEST(NetDegradedLink, AClientSurvivesSustainedLoss)
{
    NetImpairment impairment;
    impairment.LossPercent = 20;
    impairment.Seed = 20260808;

    DegradedLink link(impairment);
    ASSERT_TRUE(link.Join(600)) << "the handshake never completed through the loss";

    for (int step = 0; step < 3600; ++step)  // a minute at sixty ticks
        link.Step();

    EXPECT_TRUE(link.Client.Session.IsConnected())
        << "the client timed out on a connection that was merely lossy";
    EXPECT_EQ(link.Host.Session.PeerCount(), 1u)
        << "the authority dropped a peer that was still talking";

    const NetPeer* peer = link.Host.Session.FindPeer(link.Client.Session.LocalPeerId());
    ASSERT_NE(peer, nullptr);
    EXPECT_EQ(peer->State, NetPeerState::Connected);
    EXPECT_EQ(peer->Strikes, 0u)
        << "lost datagrams were counted against the peer as protocol violations";

    // The impairment has to have happened, or this test proves the happy path
    // under a different name.
    EXPECT_GT(link.Client.Link.Dropped(), 0u);
    EXPECT_GT(link.Host.Link.Dropped(), 0u);
}

// Latency is the impairment prediction exists for, so it has to reach the
// estimator as flight rather than being averaged away. The zero-latency run is
// the control: same code, same seed, no delay, no flight.
TEST(NetDegradedLink, LatencyBecomesTheFlightAClientStampsWith)
{
    NetImpairment delayed;
    delayed.LatencySteps = 4;  // ~67 ms each way, ~133 ms round trip
    delayed.Seed = 7;

    DegradedLink slow(delayed);
    ASSERT_TRUE(slow.Join(600));
    for (int step = 0; step < 1200; ++step)
        slow.Step();

    ASSERT_TRUE(slow.Clock.HasEstimate());
    EXPECT_GT(slow.Clock.FlightTicks(), 0u)
        << "a delayed link measured no flight, so every command would be stamped "
           "for a tick the authority has already simulated";
    EXPECT_LE(slow.Clock.FlightTicks(), 8u)
        << "flight far past half the round trip would stamp commands so far "
           "ahead they queue as standing latency";

    NetImpairment perfect;
    perfect.Seed = 7;
    DegradedLink fast(perfect);
    ASSERT_TRUE(fast.Join(600));
    for (int step = 0; step < 1200; ++step)
        fast.Step();

    ASSERT_TRUE(fast.Clock.HasEstimate());
    EXPECT_LT(fast.Clock.FlightTicks(), slow.Clock.FlightTicks())
        << "the delay made no difference to the estimate, so the flight number "
           "is not coming from the measured round trip";

    // And the whole point of knowing the flight: the stamp is ahead of the
    // authority, on the authority's own clock.
    EXPECT_GT(slow.Clock.CommandTickAt(slow.ClientTick), slow.HostTick);
}

// A jittery round trip is an ordinary connection, not a discontinuity, and the
// stamp built from it has to stay steady -- a stamp that chases the jitter
// reaches the authority's buffer as alternating gaps and backlog from a client
// whose connection was merely normal.
//
// The guarantee is one-directional on purpose. Pulling the stamp back toward the
// present is what refuses input, because a command stamped for a tick already
// simulated is dropped at the admission floor, so that direction is held to the
// slew rate. Pushing it further ahead only costs queue depth, which the buffer
// collapses, and is what a round trip that genuinely grew requires.
TEST(NetDegradedLink, AJitteryLinkNeverPullsTheStampBack)
{
    NetImpairment impairment;
    impairment.LatencySteps = 3;
    impairment.JitterSteps = 4;  // round trips varying by ~130 ms
    impairment.LossPercent = 5;
    impairment.Seed = 991;

    DegradedLink link(impairment);
    ASSERT_TRUE(link.Join(600));
    for (int step = 0; step < 6000; ++step)
        link.Step();

    ASSERT_GT(link.Observations, 4u)
        << "too few samples to say anything about how the estimate moved";

    // Two bounded movements that compound on purpose: the offset is Delta plus
    // Flight, Delta slews by at most kSlewTicks, Flight decays by at most one
    // tick, and they move together because Flight is what ages the sample Delta
    // is built from. A shrinking round trip therefore walks the stamp back at
    // two ticks per sample, which at keepalive spacing is a drift measured in
    // seconds -- not the jump that strands input.
    constexpr std::int64_t kWalkBack = NetTickEstimator::kSlewTicks + 1;
    EXPECT_LE(link.OffsetFall, kWalkBack)
        << "the stamp jumped " << link.OffsetFall << " ticks back toward the "
           "present, and every command stamped during the jump lands on a tick "
           "the authority has already run";
}

//=============================================================================
// Input, end to end
//=============================================================================

// The claim the whole command path rests on: records arrive at the rate ticks
// consume them, so any depth above the target is not input the simulation owes
// anyone -- it is latency, charged again on every tick that follows, and it will
// never drain on its own. Under real loss and real delay it has to stay shallow
// and still not starve.
TEST(NetDegradedInput, TheBufferDoesNotAccumulateStandingLatency)
{
    const InputSoak soak = RunInputSoak(BadConnection(),
                                        NetPeerCommandBuffer::kTargetDepth,
                                        /*steps*/ 3000);  // fifty seconds

    ASSERT_GT(soak.Simulated, 0u) << "no command ever reached the authority";

    // Every queued record is a tick of latency this player pays on every input
    // from then on, so the buffer has to spend its time at the target rather
    // than merely returning there.
    EXPECT_LT(soak.BackedUpRate(), 0.10)
        << "the buffer sat backed up on " << soak.BackedUp << " of "
        << soak.Simulated << " ticks: that depth is standing latency, not input "
           "owed to anyone, and it is charged again on every tick that follows";

    EXPECT_LT(soak.Deepest, NetPeerCommandBuffer::kCapacity)
        << "the buffer reached its cap at depth " << soak.Deepest
        << ", which drops the oldest record and loses input outright";

    // Starving is allowed; forgetting what the player was holding is not.
    EXPECT_EQ(soak.HeldDropped, 0u)
        << "a starved tick cleared input the player was still holding, so a "
           "player walking through a dropped packet stops walking";

    // The impairment has to have happened, or this passed as the happy path
    // under a different name.
    EXPECT_GT(soak.Dropped, 0u) << "nothing was actually lost";
    EXPECT_GT(soak.Delayed, 0u) << "nothing was actually delayed";
}

// Slack is the knob for exactly this and the only one, so it has to work.
//
// Starvation on a jittery link is not a loss problem -- eight ticks of
// redundancy behind every record makes losing one to loss alone vanishingly
// unlikely. It is an arrival-time problem: a record for tick T that has not
// landed when T is simulated is starvation whatever the reason, and jitter is
// what makes arrival times spread.
//
// What absorbs the spread is queue depth, and the way depth appears is that a
// jittery link delivers several ticks at once and the collapse stops trimming
// the queue back below the target. So raising the target raises the ceiling the
// queue is allowed to fill to, and the fuller queue is the tolerance -- paid for
// in latency, because every record waiting is a tick between the press and the
// simulation of it.
//
// Both halves of that trade are pinned here. A knob whose cost cannot be
// measured alongside its benefit is a knob nobody can tune against the stats
// panel with any confidence about what they are giving up.
TEST(NetDegradedInput, SlackBuysToleranceOfAJitteryLink)
{
    NetImpairment jittery = BadConnection();
    jittery.JitterSteps = 4;

    const InputSoak thin = RunInputSoak(jittery, 1, /*steps*/ 3000);
    const InputSoak padded = RunInputSoak(jittery, 5, /*steps*/ 3000);

    ASSERT_GT(thin.Simulated, 0u);
    ASSERT_GT(padded.Simulated, 0u);

    EXPECT_LT(padded.StarvedRate(), thin.StarvedRate() * 0.5)
        << "slack bought no tolerance: starved " << thin.StarvedRate()
        << " of ticks at one tick of margin and " << padded.StarvedRate()
        << " at five, so the knob the stats panel exposes does not do what it "
           "says";

    // And the cost side of the same trade, so the knob cannot be read as free.
    // The tolerance is bought with held records, and a held record is a tick
    // between the press and the simulation of it.
    EXPECT_GT(padded.MeanDepth(), thin.MeanDepth() + 1.0)
        << "deeper slack held no more input on an average tick (" << thin.MeanDepth()
        << " against " << padded.MeanDepth()
        << "), so the tolerance did not come from the margin and something else "
           "is paying for it";
}

// One press, on one tick, through a link losing a fifth of everything. The
// redundancy window is what gets it there; the admission floor is what keeps the
// other seven copies of it from firing it again.
TEST(NetDegradedInput, ATapSurvivesLossAndIsSimulatedExactlyOnce)
{
    NetImpairment impairment;
    impairment.LossPercent = 20;
    impairment.ReorderPercent = 5;
    impairment.LatencySteps = 3;
    impairment.JitterSteps = 3;
    impairment.Seed = 31337;

    DegradedLink link(impairment);
    ASSERT_TRUE(link.Join(600));

    NetPeerCommandBuffer buffer;
    std::uint64_t fired = 0;
    std::uint64_t simulated = 0;

    // Far enough in that the estimate has settled and the window is full.
    const std::uint64_t tapTick = link.ClientTick + 400;

    for (int step = 0; step < 1200; ++step)
    {
        link.Pump();
        ReceiveCommands(link, buffer);

        NetCommandRecord record;
        if (buffer.Next(record))
        {
            ++simulated;
            if (record.Actions[kTapAction].WasFired())
                ++fired;
        }

        SendCommand(link, tapTick);
        link.Flush();
    }

    ASSERT_GT(simulated, 0u) << "no command ever reached the authority";
    EXPECT_EQ(fired, 1u)
        << "the press was simulated " << fired << " times: zero means the "
           "redundancy window did not carry it through the loss, more than one "
           "means the admission floor let a resend of it back in";
}
