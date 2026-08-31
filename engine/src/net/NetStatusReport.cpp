#include <net/NetStatusReport.h>

#include <net/NetZoneScope.h>

#include <ecs/World.h>
#include <net/ClientPrediction.h>
#include <net/NetOwnership.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationCodec.h>
#include <net/NetSession.h>
#include <net/NetStats.h>
#include <net/NetTickEstimator.h>
#include <net/PeerCommandRuntime.h>
#include <net/ReplicationInterpolation.h>
#include <net/ReplicationRuntime.h>

#include <cinttypes>
#include <cstdio>
#include <vector>

namespace
{
    // snprintf into a fixed buffer rather than a stream: the widths here are
    // chosen so columns line up in a terminal, and iostream manipulators express
    // that much less legibly than a format string does.
    template <typename... TArgs>
    std::string Line(const char* format, TArgs... args)
    {
        char buffer[256];
        const int written = std::snprintf(buffer, sizeof(buffer), format, args...);
        if (written <= 0)
            return {};
        return std::string(buffer, static_cast<std::size_t>(
                                       std::min<std::size_t>(
                                           static_cast<std::size_t>(written),
                                           sizeof(buffer) - 1)));
    }

    std::string Bytes(double value)
    {
        if (value >= 1024.0 * 1024.0)
            return Line("%.1f MiB", value / (1024.0 * 1024.0));
        if (value >= 1024.0)
            return Line("%.1f KiB", value / 1024.0);
        return Line("%.0f B", value);
    }

    std::string Rate(const NetTrafficRate& rate)
    {
        return Bytes(rate.Bytes) + "/s " + Line("(%.1f msg/s)", rate.Messages);
    }

    std::string_view RoleName(NetSessionRole role)
    {
        switch (role)
        {
        case NetSessionRole::Host:   return "host";
        case NetSessionRole::Client: return "client";
        case NetSessionRole::Standalone: break;
        }
        return "standalone";
    }

    std::string_view FailureName(NetJoinFailure failure)
    {
        switch (failure)
        {
        case NetJoinFailure::Refused:        return "refused";
        case NetJoinFailure::TimedOut:       return "timed out";
        case NetJoinFailure::TransportError: return "transport error";
        case NetJoinFailure::Ended:          return "session ended";
        case NetJoinFailure::None:           break;
        }
        return "";
    }

    // Every section starts its content in the same column, so the report reads
    // down the labels rather than across each line looking for where the
    // numbers begin.
    constexpr int kLabelWidth = 10;
    const std::string kContinuation(2 + kLabelWidth + 1, ' ');

    void Section(std::string& out, const char* label)
    {
        out += Line("\n  %-*s ", kLabelWidth, label);
    }

    // Trailing spaces are dropped on the way out, so a section whose content
    // starts on the next line does not leave its label padding hanging.
    void Trim(std::string& out)
    {
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
    }

    void Continue(std::string& out)
    {
        Trim(out);
        out += "\n" + kContinuation;
    }

    void AppendTraffic(std::string& out, const NetStats& traffic)
    {
        Section(out, "traffic");
        out += "in  " + Rate(traffic.TotalIn());
        Continue(out);
        out += "out " + Rate(traffic.TotalOut());

        // Per kind, and only the kinds carrying something. Which traffic grew is
        // the first question anyone asks, and five rows of zeroes between the
        // two that moved is how a readout stops being read.
        for (std::size_t index = 0; index < kNetTrafficKinds; ++index)
        {
            const auto kind = static_cast<NetTrafficKind>(index);
            const NetTrafficRate in = traffic.In(kind);
            const NetTrafficRate outRate = traffic.Out(kind);
            if (in.Bytes <= 0.0 && outRate.Bytes <= 0.0)
                continue;
            const std::string_view name = NetTrafficKindToString(kind);
            Continue(out);
            out += Line("%-9.*s in %10s  out %10s",
                        static_cast<int>(name.size()), name.data(),
                        (Bytes(in.Bytes) + "/s").c_str(),
                        (Bytes(outRate.Bytes) + "/s").c_str());
        }

        Continue(out);
        out += "lifetime " + Bytes(static_cast<double>(traffic.LifetimeBytesIn()))
             + " in, " + Bytes(static_cast<double>(traffic.LifetimeBytesOut()))
             + " out";
    }

    void AppendReplication(std::string& out, const ReplicationRuntime& replication)
    {
        const ReplicationRuntime::PublishStats& publish = replication.LastPublish();
        Section(out, "publish");
        if (publish.SnapshotsSent == 0)
        {
            out += "nothing published yet";
            return;
        }

        const double occupancy =
            publish.BudgetBytes == 0
                ? 0.0
                : 100.0 * static_cast<double>(publish.PeakSnapshotBytes)
                      / static_cast<double>(publish.BudgetBytes);
        // Occupancy well under the budget with entities still deferred means
        // something other than bytes is doing the limiting, which is the one
        // reading of these two numbers that sends someone somewhere else.
        out += Line("%u snapshot(s) to %u peer(s); peak %zu of %zu B (%.0f%%)",
                    publish.SnapshotsSent, publish.PeersServed,
                    publish.PeakSnapshotBytes, publish.BudgetBytes, occupancy);
        Continue(out);
        out += Line("%u deferred, oldest %u snapshot(s) behind; %u destroy(s) "
                    "deferred", publish.EntitiesDeferred,
                    publish.OldestDeferredSnapshots, publish.DestroysDeferred);

        // Seeding is the expensive half of a join and the half that is supposed
        // to stop. A body still mostly seeding well after one is a peer that is
        // not converging, which reads as plain bandwidth from a total.
        Continue(out);
        out += Line("%zu B seeding, %zu B delta", publish.SeedingBytes,
                    publish.DeltaBytes);

        std::string costliest;
        for (const SnapshotWriteResult::EntityCost& cost : publish.Costliest)
        {
            if (cost.Bits == 0)
                break;
            if (!costliest.empty())
                costliest += ", ";
            costliest += Line("%" PRIu64 " (%zu B)", cost.Id.Value, cost.Bits / 8);
        }
        if (!costliest.empty())
        {
            Continue(out);
            out += "costliest " + costliest;
        }

        if (publish.EntitiesUnsendable > 0)
        {
            // Not back-pressure. Nothing about these can reach a peer at any
            // budget they would fit in, so waiting will not help.
            Continue(out);
            out += Line("! %u entity(s) too large for a snapshot -- raise "
                        "net.snapshot_bytes", publish.EntitiesUnsendable);
        }
    }

    std::string ChannelHealth(const NetChannelSet& channels)
    {
        return Line("%zu outstanding, %" PRIu64 " resent, %" PRIu64 " stale, %"
                    PRIu64 " duplicate",
                    channels.OutstandingReliable(), channels.Resends(),
                    channels.StaleDropped(), channels.DuplicatesIgnored());
    }

    void AppendPeers(std::string& out, const NetSession& session,
                     const PeerCommandRuntime* commands)
    {
        const std::vector<PeerId> peers = session.ConnectedPeers();
        Section(out, "peers");
        out += std::to_string(peers.size());
        if (peers.empty())
            return;

        Continue(out);
        out += Line("%-5s %8s %8s %7s %8s", "peer", "rtt", "strikes", "queued",
                    "starved");
        for (const PeerId peer : peers)
        {
            const NetPeer* record = session.FindPeer(peer);
            const NetPeerCommandBuffer* input =
                commands == nullptr ? nullptr : commands->Peer(peer);
            Continue(out);
            out += Line(
                "%-5u %6.1fms %8u %7zu %8" PRIu64, peer.Value,
                record == nullptr
                    ? 0.0
                    : static_cast<double>(record->RoundTripMicroseconds) / 1000.0,
                record == nullptr ? 0u : record->Strikes,
                input == nullptr ? std::size_t{ 0 } : input->QueuedTicks(),
                input == nullptr ? std::uint64_t{ 0 } : input->StarvedTicks());
            if (record != nullptr)
            {
                Continue(out);
                out += "  channel " + ChannelHealth(record->Channels);
            }
        }
    }

    void AppendApply(std::string& out, const ReplicationRuntime& replication)
    {
        const SnapshotApplyResult& applied = replication.LastApply();
        Section(out, "applied");
        if (!applied.Ok())
        {
            out += "refused: "
                 + std::string(SnapshotApplyErrorToString(applied.Error));
            return;
        }
        out += Line("tick %" PRIu64 "; %u spawned, %u updated, %u destroyed, "
                    "%u component(s) removed",
                    applied.Tick, applied.EntitiesSpawned, applied.EntitiesUpdated,
                    applied.EntitiesDestroyed, applied.ComponentsRemoved);

        // The two ways an entity the wire named is not here yet. Both go
        // unacknowledged deliberately, so the authority describes them again:
        // an authored key this machine has not loaded, and a prefab it cannot
        // build. A count that never falls is content the two ends disagree
        // about, and the log names which prefab.
        if (applied.PrefabsDeferred != 0 || applied.AuthoredDeferred != 0
            || applied.AuthoredBound != 0)
        {
            Continue(out);
            out += Line("%u prefab(s) deferred, %u authored bound, %u deferred",
                        applied.PrefabsDeferred, applied.AuthoredBound,
                        applied.AuthoredDeferred);
        }
    }

    void AppendPrediction(std::string& out, const ClientPrediction& prediction)
    {
        Section(out, "prediction");
        if (!prediction.Predicted().IsValid())
        {
            out += "no pawn yet";
        }
        else if (!prediction.IsEnabled())
        {
            out += "off (net.prediction) -- input costs a round trip";
        }
        else
        {
            // Reset distance is what says whether this is working: a steady few
            // centimetres is healthy, a rising floor is the two simulations
            // drifting apart faster than they are pulled together.
            out += Line("reset %.3f m, %u tick(s) replayed; %" PRIu64
                        " reconcile(s), %" PRIu64 " snap(s)",
                        static_cast<double>(prediction.LastResetMeters()),
                        prediction.LastReplayedTicks(), prediction.Reconciles(),
                        prediction.Snaps());
        }
    }

    void AppendInterpolation(std::string& out,
                             const ReplicationInterpolation& interpolation)
    {
        Section(out, "interp");
        if (!interpolation.IsEnabled())
        {
            out += "off (net.interpolation) -- others step as snapshots land";
            return;
        }
        if (interpolation.TrackedCount() == 0)
        {
            out += "nothing mirrored yet";
            return;
        }
        // Held ticks are the tell. A rising share means the presented tick runs
        // past the newest sample, which is the delay being too short for this
        // connection rather than anything going wrong here.
        out += Line("%zu mirrored, %u tick delay; %" PRIu64 " blended, %" PRIu64
                    " held of %" PRIu64,
                    interpolation.TrackedCount(), interpolation.DelayTicks(),
                    interpolation.Interpolated(), interpolation.Held(),
                    interpolation.Resolved());
    }
}

std::string NetFormatStatus(const NetStatusSources& sources)
{
    const NetSession* session = sources.Session;
    if (session == nullptr)
        return "standalone (no session)";

    const std::string_view role = RoleName(session->Role());
    std::string out(role);
    out += " at " + NetAddressToString(session->LocalAddress());
    out += Line("; tick %" PRIu64, session->LocalTick());

    if (session->Role() == NetSessionRole::Client)
    {
        out += session->IsConnected()
                   ? "; admitted as peer "
                         + std::to_string(session->LocalPeerId().Value)
                   : "; not admitted";
        if (session->RoundTripMicroseconds() > 0)
        {
            out += Line("; rtt %.1fms",
                        static_cast<double>(session->RoundTripMicroseconds())
                            / 1000.0);
        }
    }

    if (session->JoinFailure() != NetJoinFailure::None)
    {
        out += "; " + std::string(FailureName(session->JoinFailure())) + ": "
             + session->JoinFailureReason();
    }
    out += Line("; %" PRIu64 " strike(s), %" PRIu64 " refusal(s)",
                session->StrikesIssued(), session->Refusals());

    if (sources.Traffic != nullptr)
        AppendTraffic(out, *sources.Traffic);

    if (session->Role() == NetSessionRole::Host)
    {
        if (sources.Replication != nullptr)
            AppendReplication(out, *sources.Replication);
        AppendPeers(out, *session, sources.Commands);
    }
    else
    {
        // A client's own view of the same questions: what the last snapshot
        // did to it, what it is guessing, what it is presenting, and whether
        // its one channel is backing up.
        if (sources.Replication != nullptr)
            AppendApply(out, *sources.Replication);
        if (sources.Prediction != nullptr)
            AppendPrediction(out, *sources.Prediction);
        if (sources.Interpolation != nullptr)
            AppendInterpolation(out, *sources.Interpolation);
        if (sources.Clock != nullptr && sources.Clock->HasEstimate())
        {
            // Input stamped for a tick the authority has already run is input
            // thrown away, so how far ahead this machine is sending is the
            // number behind a client whose commands are correct and ignored.
            const std::uint64_t local = session->LocalTick();
            Section(out, "clock");
            out += Line("authority %" PRIu64 ", sending for %" PRIu64 " (+%d)",
                        sources.Clock->AuthorityTickAt(local),
                        sources.Clock->CommandTickAt(local),
                        static_cast<int>(sources.Clock->CommandOffset()
                                         - sources.Clock->Offset()));
        }
        if (const NetChannelSet* channels = session->AuthorityChannels())
        {
            Section(out, "channel");
            out += "to host " + ChannelHealth(*channels);
        }
    }

    Trim(out);
    return out;
}

//=============================================================================
// One object
//=============================================================================

namespace
{
    // A component's field runs by name, for a mask. The dotted names are what
    // net_components prints and what a schema declares, so the answer here can
    // be taken straight back to the declaration that produced it.
    std::string FieldNames(const ReplicatedComponent& component, std::uint64_t mask)
    {
        if (mask == 0)
            return "-";
        std::string names;
        for (std::size_t run = 0; run < component.Fields.size(); ++run)
        {
            if ((mask & (std::uint64_t{ 1 } << run)) == 0)
                continue;
            if (!names.empty())
                names += ", ";
            names += component.Fields[run].Name;
        }
        return names;
    }

    void AppendEntityPeers(std::string& out, const NetSession& session,
                           const ReplicationRuntime& replication,
                           const ReplicationLayout& layout,
                           const ReplicationChangeStore::EntityState& held,
                           PeerId focus)
    {
        Section(out, "peers");
        Continue(out);
        out += Line("%-5s %5s %10s %10s  %s", "peer", "owner", "floor",
                    "last sent", "owed");

        for (const PeerId peer : session.ConnectedPeers())
        {
            if (focus.IsValid() && peer != focus)
                continue;

            const ReplicationPeerState* baseline = replication.PeerBaseline(peer);
            const std::uint64_t floor =
                baseline == nullptr ? 0 : baseline->Floor(held.Id);
            const std::uint32_t sentAt =
                baseline == nullptr ? 0 : baseline->LastSentAt(held.Id);
            const bool isOwner = held.Owner == peer.Value;
            const bool ownershipMoved = held.OwnerChangedAt > floor;

            // What this peer is still owed, by the writer's own rule. An empty
            // answer beside a floor below the store's generation is the shape
            // of an entity nothing is left to say about; an answer that never
            // empties is the one worth chasing.
            std::string owed;
            for (const ReplicationChangeStore::ComponentState& state : held.Components)
            {
                const ReplicatedComponent* component = layout.At(state.WireIndex);
                if (component == nullptr)
                    continue;
                const std::uint64_t mask = ReplicationOwedFields(
                    *component, state, floor, ownershipMoved, isOwner);
                if (mask == 0)
                    continue;
                if (!owed.empty())
                    owed += "; ";
                owed += std::string(component->Name) + ":"
                      + FieldNames(*component, mask);
            }

            Continue(out);
            out += Line("%-5u %5s %10" PRIu64 " %10u  %s", peer.Value,
                        isOwner ? "yes" : "-", floor, sentAt,
                        owed.empty() ? "-" : owed.c_str());

            // A peer that has confirmed nothing has not necessarily been sent
            // nothing, and the two read very differently when the entity is
            // missing on that machine.
            if (floor == 0 && sentAt != 0)
            {
                Continue(out);
                out += "      sent but never confirmed -- every part of it is "
                       "still owed";
            }
        }
    }
}

std::string NetFormatEntity(const NetEntityReportSources& sources, NetEntityId id,
                            PeerId focus)
{
    const NetSession* session = sources.Session;
    if (session == nullptr || sources.Replication == nullptr)
        return "standalone (no session)";
    if (!id.IsValid())
        return "not a network id";

    const ReplicationRuntime& replication = *sources.Replication;
    std::string out = Line("net entity %" PRIu64, id.Value);

    if (session->Role() != NetSessionRole::Host)
    {
        // A client holds the mapping and none of the history: what it was told
        // is on the entity, and why it was told is the authority's business.
        const EntityId local = replication.ClientEntities().TryResolve(id);
        Section(out, "client");
        out += local.IsValid()
                   ? Line("entity %u:%u", local.Index, local.Generation)
                   : std::string("not one this machine has been given");
        return out;
    }

    const EntityId local = replication.AuthorityEntities().TryResolve(id);
    const ReplicationChangeStore::EntityState* held =
        replication.PublishedState().Find(id);

    if (held == nullptr)
    {
        Section(out, "gone");
        out += local.IsValid()
                   ? "released from the world but the identity is still bound"
                   : "the authority has no record of it";
        // Which peers have yet to be told, which is the difference between an
        // entity that is finished with and one still being taken away.
        std::string owing;
        for (const PeerId peer : session->ConnectedPeers())
        {
            const ReplicationPeerState* baseline = replication.PeerBaseline(peer);
            if (baseline == nullptr)
                continue;
            for (const NetEntityId owed : baseline->OwedDestroys())
            {
                if (owed != id)
                    continue;
                if (!owing.empty())
                    owing += ", ";
                owing += std::to_string(peer.Value);
            }
        }
        if (!owing.empty())
        {
            Continue(out);
            out += "destroy still owed to peer(s) " + owing;
        }
        return out;
    }

    Section(out, "entity");
    out += local.IsValid() ? Line("%u:%u", local.Index, local.Generation)
                           : std::string("no local handle");
    out += held->Owner == 0
               ? std::string("; authority-owned")
               : Line("; owned by peer %u (moved at gen %" PRIu64 ")",
                      held->Owner, held->OwnerChangedAt);
    out += Line("; seen at gen %" PRIu64 " of %" PRIu64, held->SeenAt,
                replication.PublishedState().Generation());

    if (held->Persistent.IsValid())
    {
        Section(out, "authored");
        out += Line("persistent 0x%016" PRIx64, held->Persistent.Value);
    }

    if (sources.Layout != nullptr)
    {
        const ReplicationLayout& layout = *sources.Layout;
        Section(out, "state");
        for (const ReplicationChangeStore::ComponentState& state : held->Components)
        {
            const ReplicatedComponent* component = layout.At(state.WireIndex);
            if (component == nullptr)
                continue;
            Continue(out);
            // Per run rather than per component: a rotating entity that is not
            // moving sends rotation alone, so which run moved is the answer and
            // "the transform changed" is not.
            out += Line("%-3u %s", state.WireIndex,
                        std::string(component->Name).c_str());
            for (std::size_t run = 0; run < component->Fields.size(); ++run)
            {
                Continue(out);
                out += Line("      %-24s moved at gen %" PRIu64,
                            component->Fields[run].Name.c_str(),
                            run < state.ChangedAt.size() ? state.ChangedAt[run]
                                                         : std::uint64_t{ 0 });
            }
        }

        for (const ReplicationChangeStore::RemovedComponent& removed : held->Removed)
        {
            const ReplicatedComponent* component = layout.At(removed.WireIndex);
            Section(out, "removed");
            out += Line("%-3u %s at gen %" PRIu64, removed.WireIndex,
                        component == nullptr ? "?"
                                             : std::string(component->Name).c_str(),
                        removed.RemovedAt);
        }

        AppendEntityPeers(out, *session, replication, layout, *held, focus);
    }

    Trim(out);
    return out;
}

//=============================================================================
// Network ownership
//=============================================================================

std::string NetFormatOwners(const NetSession* session, const World& entities,
                            const ReplicationRuntime* replication)
{
    std::string out;
    if (session == nullptr || session->Role() != NetSessionRole::Host)
        return "network ownership requires a host session";

    std::vector<EntityId> owned;
    for (const PeerId peer : session->ConnectedPeers())
    {
        NetOwnedBy(entities, peer, owned);
        Section(out, "peer");
        out += Line("%u owns %zu", peer.Value, owned.size());
        for (const EntityId entity : owned)
        {
            Continue(out);
            out += Line("  entity %u:%u", entity.Index, entity.Generation);
            if (replication != nullptr)
            {
                const NetEntityId id =
                    replication->AuthorityEntities().TryFind(entity);
                // An owned entity that replication has never named is one no
                // peer can be told about, which is the failure that looks like
                // a possession working on the host and nowhere else.
                out += id.IsValid() ? Line("  net %" PRIu64, id.Value)
                                    : std::string("  NOT REPLICATED");
            }
        }
    }
    Trim(out);
    return out;
}

namespace
{
    const char* ZoneStateName(NetZoneScopeState state)
    {
        switch (state)
        {
        case NetZoneScopeState::None:    break;
        case NetZoneScopeState::Granted: return "loading";
        case NetZoneScopeState::Acked:   return "open";
        }
        return "none";
    }

    void ZoneLines(std::string& out, const NetZoneScope& scope)
    {
        if (scope.Size() == 0)
        {
            out += std::string("no zones");
            return;
        }
        bool first = true;
        for (const NetZoneScope::Entry& entry : scope.Entries())
        {
            if (!first)
                Continue(out);
            first = false;
            out += Line("  %016" PRIx64 " %s", entry.Zone.Value,
                        ZoneStateName(entry.State));
        }
    }
}

std::string NetFormatZones(const NetSession* session,
                           const ReplicationRuntime* replication)
{
    if (session == nullptr || replication == nullptr)
        return "standalone (no session)";

    std::string out;

    if (session->Role() == NetSessionRole::Host)
    {
        // Which zones are gated at all, and only on the machine that gates
        // them. Empty is not a fault: a session that loaded a map rather than
        // streaming a world has one room no policy names, and nothing about it
        // is withheld from anybody.
        const std::span<const ZoneId> streamed = replication->StreamedZones();
        Section(out, "streamed");
        out += streamed.empty()
                   ? std::string("none (nothing is zone-gated)")
                   : Line("%zu zone(s) under scope control", streamed.size());

        for (const PeerId peer : session->ConnectedPeers())
        {
            const ReplicationPeerState* baseline = replication->PeerBaseline(peer);
            Section(out, "peer");
            if (baseline == nullptr)
            {
                out += Line("%u has no baseline yet", peer.Value);
                continue;
            }
            out += Line("%u holds %zu", peer.Value, baseline->Zones().Size());
            Continue(out);
            ZoneLines(out, baseline->Zones());
        }
        Trim(out);
        return out;
    }

    // A client has one authority, so it has one answer. "loading" here and
    // "loading" on the host mean different things a moment apart: this machine
    // has been told to hold the room, and the host has not been told it does.
    Section(out, "granted");
    ZoneLines(out, replication->LocalZones());
    Trim(out);
    return out;
}
