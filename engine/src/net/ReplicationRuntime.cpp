#include <net/ReplicationRuntime.h>

#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>

#include <algorithm>

namespace
{
    // One kind byte, then the bit-packed snapshot. A snapshot rides the
    // unreliable channel, so it has to fit one datagram; there is no
    // fragmentation to fall back on and no point resending, because the next
    // snapshot supersedes this one before a resend could arrive.
    constexpr std::size_t kKindBytes = 1;
    constexpr std::size_t kMaxSnapshotBytes = kNetMaxPayloadBytes - kKindBytes;
}

ReplicationRuntime::PublishStats ReplicationRuntime::Publish(
    NetSession& session, World& world, const ReplicationLayout& layout,
    std::uint64_t tick, const PeerCommandRuntime* commands)
{
    PublishStats stats;
    if (session.Role() != NetSessionRole::Host)
        return stats;

    const std::vector<PeerId> peers = session.ConnectedPeers();

    // Peers that left between frames stop costing a baseline. Ahead of the
    // cadence gate rather than behind it, because a peer's memory should be
    // released when it leaves and not on whatever tick the next snapshot
    // happens to fall on. Done here rather than only on the leave event so a
    // missed event cannot leak.
    std::erase_if(Peers, [&peers](const auto& entry) {
        return std::find(peers.begin(), peers.end(), entry.first) == peers.end();
    });

    if (peers.empty())
        return stats;

    // Publishing is paced in simulation ticks, and the pace is held by
    // difference rather than by remainder: a frame that ran several ticks moves
    // the index by several, and a stride the index never lands on exactly would
    // silence this authority permanently. A tick behind the last publish is a
    // clock discontinuity (a reset, a rewound recording) and publishes.
    const std::uint32_t interval = std::max<std::uint32_t>(1, PublishInterval);
    if (HasPublished && tick >= LastPublishedTick
        && (tick - LastPublishedTick) < interval)
    {
        return stats;
    }
    LastPublishedTick = tick;
    HasPublished = true;

    if (Scratch.size() < kKindBytes + kMaxSnapshotBytes)
        Scratch.resize(kKindBytes + kMaxSnapshotBytes);
    Scratch[0] = static_cast<std::byte>(NetPayloadKind::Snapshot);

    for (PeerId peer : peers)
    {
        ++stats.PeersServed;
        // A peer seen for the first time has no baseline, so its first
        // snapshot is full state. Nothing special-cases a join.
        ReplicationPeerState& baseline = Peers[peer];

        SnapshotWriteRequest request;
        request.Source = &world;
        request.Layout = &layout;
        request.Identity = &Identity;
        request.Peer = &baseline;
        request.OwnerPeer = peer.Value;
        request.Tick = tick;
        request.Sequence = baseline.NextSnapshotSequence();
        request.CommandAck = commands == nullptr ? 0 : commands->AckFor(peer);

        // What this peer has proved it holds, before the difference against it
        // is computed. The bound on how far it may fall behind is the peer
        // state's own business.
        if (commands != nullptr)
            baseline.Acknowledge(commands->SnapshotAckFor(peer));

        const SnapshotWriteResult written = ReplicationWriteSnapshot(
            request, std::span(Scratch).subspan(kKindBytes, kMaxSnapshotBytes));
        if (!written.Ok)
        {
            // Over budget for one datagram. The baseline was left untouched, so
            // the next attempt still describes the same difference rather than
            // one against a snapshot this peer never got.
            continue;
        }

        const std::size_t total = kKindBytes + written.BytesWritten;
        if (!session.Send(peer, NetChannelKind::UnreliableSequenced,
                          std::span(Scratch).subspan(0, total)))
        {
            continue;
        }

        ++stats.SnapshotsSent;
        stats.BytesQueued += total;
    }

    return stats;
}

SnapshotApplyResult ReplicationRuntime::Apply(std::span<const std::byte> payload,
                                              World& world,
                                              const WorldComponentSchema& schema,
                                              const ReplicationLayout& layout,
                                              const NetSpawnRecipes* recipes,
                                              ClientPrediction* prediction,
                                              ReplicationInterpolation* interpolation)
{
    SnapshotApplyResult result;
    if (payload.size() < kKindBytes)
    {
        result.Error = SnapshotApplyError::Truncated;
        return result;
    }
    if (static_cast<NetPayloadKind>(payload[0]) != NetPayloadKind::Snapshot)
        return result;  // Another kind on the same channel; not ours, not an error.

    SnapshotApplyRequest request;
    request.Target = &world;
    request.Schema = &schema;
    request.Layout = &layout;
    request.Identity = &ClientMap;
    request.Recipes = recipes;
    request.Prediction = prediction;
    request.Interpolation = interpolation;

    const SnapshotApplyResult applied =
        ReplicationApplySnapshot(request, payload.subspan(kKindBytes));
    // Only a snapshot that applied cleanly counts as one this machine holds; a
    // refused one left the world part-way and must not be acknowledged.
    if (applied.Ok())
    {
        AppliedTick = std::max(AppliedTick, applied.Tick);
        AppliedAcks.Observe(applied.Sequence);
    }
    return applied;
}

void ReplicationRuntime::ForgetPeer(PeerId peer)
{
    Peers.erase(peer);
}

void ReplicationRuntime::Reset()
{
    Identity = ReplicationAuthorityIdentity{};
    Peers.clear();
    ClientMap.Clear();
    AppliedTick = 0;
    AppliedAcks.Clear();
    LastPublishedTick = 0;
    HasPublished = false;
}
