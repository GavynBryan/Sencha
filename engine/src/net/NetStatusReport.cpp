#include <net/NetStatusReport.h>

#include <net/ClientPrediction.h>
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

    void Continue(std::string& out)
    {
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
        out += Line("%u deferred, oldest %u snapshot(s) behind",
                    publish.EntitiesDeferred, publish.OldestDeferredSnapshots);

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
        // A client's own view of the same three questions: what it is guessing,
        // what it is presenting, and whether its one channel is backing up.
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

    return out;
}
