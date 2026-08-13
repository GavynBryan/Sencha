#include <net/ReplicationRuntime.h>

#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>

#include <algorithm>
#include <array>

namespace
{
    // Folds one peer's costliest entity into the publish-wide list, keeping the
    // largest a single peer paid and never counting an entity twice.
    void MergeCost(std::array<SnapshotWriteResult::EntityCost,
                              SnapshotWriteResult::kCostliestTracked>& top,
                   const SnapshotWriteResult::EntityCost& cost)
    {
        for (SnapshotWriteResult::EntityCost& held : top)
        {
            if (held.Id != cost.Id)
                continue;
            held.Bits = std::max(held.Bits, cost.Bits);
            std::sort(top.begin(), top.end(),
                      [](const auto& a, const auto& b) {
                          if (a.Bits != b.Bits)
                              return a.Bits > b.Bits;
                          return a.Id.Value < b.Id.Value;
                      });
            return;
        }
        if (cost.Bits <= top.back().Bits && top.back().Bits != 0)
            return;
        top.back() = cost;
        std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) {
            if (a.Bits != b.Bits)
                return a.Bits > b.Bits;
            return a.Id.Value < b.Id.Value;
        });
    }
}

void ReplicationRuntime::SetSnapshotBytes(std::size_t bytes)
{
    Budget = std::clamp(bytes, kNetMinSnapshotBytes, kNetMaxSnapshotBytes);
}

ReplicationRuntime::PublishStats ReplicationRuntime::Publish(
    NetSession& session, World& world, const ReplicationLayout& layout,
    std::uint64_t tick, const PeerCommandRuntime* commands,
    const RuntimeWorld* zones)
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

    // Sized to the largest budget rather than the current one, so changing the
    // budget mid-session does not reallocate and a lowered one simply uses less
    // of the same buffer.
    if (Scratch.size() < kNetPayloadKindBytes + kNetMaxSnapshotBytes)
        Scratch.resize(kNetPayloadKindBytes + kNetMaxSnapshotBytes);
    Scratch[0] = static_cast<std::byte>(NetPayloadKind::Snapshot);
    stats.BudgetBytes = Budget;

    // What the world looks like, and what moved since last time -- computed
    // once and read by every peer. The generation is this store's own count of
    // publishes, not the simulation tick: it has to increase on every pass and
    // the tick does not (a paused authority still publishes).
    Changes.Update(world, layout, Identity, ++Generation, zones);

    for (PeerId peer : peers)
    {
        ++stats.PeersServed;
        // A peer seen for the first time has no baseline, so its first
        // snapshot is full state. Nothing special-cases a join.
        ReplicationPeerState& baseline = Peers[peer];

        SnapshotWriteRequest request;
        request.Changes = &Changes;
        request.Layout = &layout;
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

        // The subspan is the budget: what does not fit is deferred to a later
        // snapshot rather than failing this one.
        const SnapshotWriteResult written = ReplicationWriteSnapshot(
            request, std::span(Scratch).subspan(kNetPayloadKindBytes, Budget));
        stats.EntitiesDeferred += written.EntitiesDeferred;
        stats.EntitiesUnsendable += written.EntitiesUnsendable;
        stats.OldestDeferredSnapshots = std::max(stats.OldestDeferredSnapshots,
                                                 written.OldestDeferredSnapshots);
        stats.PeakSnapshotBytes =
            std::max(stats.PeakSnapshotBytes, written.BytesWritten);
        stats.DestroysDeferred += written.DestroysDeferred;
        stats.SeedingBytes += written.SeedingBits / 8;
        stats.DeltaBytes += written.DeltaBits / 8;
        // The most any one peer paid for an entity, not the sum: a world of
        // sixteen peers would otherwise report every entity as sixteen times
        // its size and rank them all identically.
        for (const SnapshotWriteResult::EntityCost& cost : written.Costliest)
        {
            if (cost.Bits == 0)
                break;
            MergeCost(stats.Costliest, cost);
        }
        if (!written.Ok)
        {
            // Nothing a world can do reaches here -- the writer fills to the
            // budget and reports the remainder -- so this is a malformed request
            // or a writer that disagreed with its own measurement. The peer
            // state is left untouched either way.
            continue;
        }

        const std::size_t total = kNetPayloadKindBytes + written.BytesWritten;
        if (!session.Send(peer, NetChannelKind::UnreliableSequenced,
                          std::span(Scratch).subspan(0, total)))
        {
            continue;
        }

        ++stats.SnapshotsSent;
        stats.BytesQueued += total;
    }

    Published = stats;
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
    if (payload.size() < kNetPayloadKindBytes)
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
        ReplicationApplySnapshot(request, payload.subspan(kNetPayloadKindBytes));
    // Only a snapshot this machine holds in full counts as one it holds. A
    // refused one carried something this build cannot read; a deferred one
    // named an authored entity the level has not produced yet. Acknowledging
    // either would raise the authority's floor past state that never landed,
    // and a floor only ever moves forward -- so the entity would stop being
    // described and the two would disagree about it for the session.
    if (applied.Complete())
    {
        AppliedTick = std::max(AppliedTick, applied.Tick);
        AppliedAcks.Observe(applied.Sequence);
    }
    // Held whether or not it was accepted: a refused snapshot is exactly the
    // one somebody wants to see the reason for.
    Applied = applied;
    return applied;
}

ReplicationRuntime::ZoneScopeStats ReplicationRuntime::PublishZoneScope(
    NetSession& session, std::span<const NetPeerZoneInterest> interest)
{
    ZoneScopeStats stats;
    if (session.Role() != NetSessionRole::Host)
        return stats;

    // Small and fixed: one verb and one zone id.
    std::array<std::byte, 32> scratch{};
    std::vector<ZoneId> revoking;

    for (const PeerId peer : session.ConnectedPeers())
    {
        ReplicationPeerState& baseline = Peers[peer];
        NetZoneScope& scope = baseline.Zones();

        std::span<const ZoneId> wanted;
        for (const NetPeerZoneInterest& record : interest)
        {
            if (record.Peer == peer)
            {
                wanted = record.Zones;
                break;
            }
        }

        const auto send = [&](ZoneId zone, NetZoneScopeVerb verb) {
            const std::size_t size = NetEncodeZoneScopeUpdate(
                NetZoneScopeUpdate{ .Zone = zone, .Verb = verb }, scratch);
            if (size == 0)
                return false;
            if (!session.Send(peer, NetChannelKind::ReliableOrdered,
                              std::span<const std::byte>(scratch).subspan(0, size)))
            {
                return false;
            }
            stats.BytesQueued += size;
            return true;
        };

        // Revokes are collected before they are applied, because dropping an
        // entry invalidates the walk over the same storage.
        revoking.clear();
        for (const NetZoneScope::Entry& held : scope.Entries())
        {
            const bool stillWanted = std::binary_search(
                wanted.begin(), wanted.end(), held.Zone,
                [](ZoneId a, ZoneId b) { return a.Value < b.Value; });
            if (!stillWanted)
                revoking.push_back(held.Zone);
        }

        // Revoked before granted, so a peer at its own residency cap is told
        // what it may let go before it is told to take more on.
        for (const ZoneId zone : revoking)
        {
            if (!send(zone, NetZoneScopeVerb::Revoke))
                continue;
            (void)scope.Revoke(zone);
            ++stats.Revokes;
        }

        for (const ZoneId zone : wanted)
        {
            if (!zone.IsValid() || scope.StateOf(zone) != NetZoneScopeState::None)
                continue;
            // Sent before the state moves: a grant the channel refused would
            // otherwise leave the authority believing a room is loading that
            // the peer was never told about, and nothing would ever say it
            // again.
            if (!send(zone, NetZoneScopeVerb::Grant))
                continue;
            (void)scope.Grant(zone);
            ++stats.Grants;
        }
    }

    return stats;
}

bool ReplicationRuntime::AcknowledgeZone(PeerId peer, ZoneId zone)
{
    const auto it = Peers.find(peer);
    if (it == Peers.end())
        return false;
    return it->second.Zones().Acknowledge(zone);
}

void ReplicationRuntime::ApplyZoneScope(const NetZoneScopeUpdate& update)
{
    switch (update.Verb)
    {
    case NetZoneScopeVerb::Grant:
        (void)LocalScope.Grant(update.Zone);
        break;
    case NetZoneScopeVerb::Revoke:
        (void)LocalScope.Revoke(update.Zone);
        break;
    }
}

ReplicationRuntime::ZoneAckStats ReplicationRuntime::AcknowledgeResidentZones(
    NetSession& session, const RuntimeWorld& world)
{
    ZoneAckStats stats;
    if (session.Role() != NetSessionRole::Client || !session.IsConnected())
        return stats;

    std::array<std::byte, 32> scratch{};

    // Collected before any of it is confirmed: acknowledging mutates the same
    // storage the walk is reading.
    std::vector<ZoneId> ready;
    for (const NetZoneScope::Entry& held : LocalScope.Entries())
    {
        if (held.State == NetZoneScopeState::Granted
            && world.IsZoneResident(held.Zone))
        {
            ready.push_back(held.Zone);
        }
    }

    for (const ZoneId zone : ready)
    {
        const std::size_t size = NetEncodeZoneAck(zone, scratch);
        if (size == 0)
            continue;
        // A client reaches its authority by sending to its own id, the same way
        // its input does. Reliable: a lost ack is a room the authority never
        // fills, and there is no next one to supersede it.
        if (!session.Send(session.LocalPeerId(), NetChannelKind::ReliableOrdered,
                          std::span<const std::byte>(scratch).subspan(0, size)))
        {
            continue;
        }
        // Marked only once the confirmation is actually queued, so a refused
        // send is retried next frame rather than leaving the authority waiting
        // on an ack that was never written.
        (void)LocalScope.Acknowledge(zone);
        ++stats.Acks;
        stats.BytesQueued += size;
    }

    return stats;
}

const ReplicationPeerState* ReplicationRuntime::PeerBaseline(PeerId peer) const
{
    const auto it = Peers.find(peer);
    return it == Peers.end() ? nullptr : &it->second;
}

void ReplicationRuntime::ForgetPeer(PeerId peer)
{
    Peers.erase(peer);
}

void ReplicationRuntime::Reset()
{
    Identity = ReplicationAuthorityIdentity{};
    Changes.Reset();
    Generation = 0;
    Peers.clear();
    LocalScope.Clear();
    ClientMap.Clear();
    Applied = SnapshotApplyResult{};
    AppliedTick = 0;
    AppliedAcks.Clear();
    LastPublishedTick = 0;
    HasPublished = false;
    Published = PublishStats{};
}
