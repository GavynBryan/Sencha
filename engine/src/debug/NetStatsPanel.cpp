#include <debug/NetStatsPanel.h>

#include <core/console/ConsoleRegistry.h>
#include <net/ClientPrediction.h>
#include <net/NetSession.h>
#include <net/NetStats.h>
#include <net/NetTickEstimator.h>
#include <net/PeerCommandRuntime.h>
#include <net/ReplicationRuntime.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cinttypes>

namespace
{
    const char* RoleLabel(NetSessionRole role)
    {
        switch (role)
        {
        case NetSessionRole::Host:   return "host";
        case NetSessionRole::Client: return "client";
        case NetSessionRole::Standalone: break;
        }
        return "standalone";
    }

    // Rates are read at a glance, so they are scaled to the unit that keeps
    // them legible rather than printed at full precision in one unit.
    void RateText(const char* label, const NetTrafficRate& rate)
    {
        if (rate.Bytes >= 1024.0)
        {
            ImGui::Text("%-9s %7.1f KiB/s  %6.1f msg/s", label,
                        rate.Bytes / 1024.0, rate.Messages);
        }
        else
        {
            ImGui::Text("%-9s %7.0f B/s    %6.1f msg/s", label, rate.Bytes,
                        rate.Messages);
        }
    }

    void PlotBytes(const char* label, std::span<const float> series)
    {
        if (series.empty())
        {
            ImGui::TextUnformatted("collecting...");
            return;
        }
        const float peak = *std::max_element(series.begin(), series.end());
        ImGui::PlotLines(label, series.data(), static_cast<int>(series.size()),
                         0, nullptr, 0.0f, std::max(peak, 1.0f),
                         ImVec2(0.0f, 48.0f));
    }
}

NetStatsPanel::NetStatsPanel(const std::unique_ptr<NetSession>& session,
                             const NetStats& traffic,
                             const NetTickEstimator& clock,
                             const ClientPrediction& prediction,
                             const ReplicationInterpolation& interpolation,
                             const ReplicationRuntime& replication,
                             PeerCommandRuntime& commands,
                             ConsoleRegistry& console)
    : Session(session)
    , Traffic(traffic)
    , Clock(clock)
    , Prediction(prediction)
    , Interpolation(interpolation)
    , Replication(replication)
    , Commands(commands)
    , Console(console)
{
}

void NetStatsPanel::Draw()
{
    if (!ImGui::Begin("Net"))
    {
        ImGui::End();
        return;
    }

    const NetSession* session = Session.get();
    if (session == nullptr)
    {
        ImGui::TextUnformatted("No session. `host <port>` or `connect <addr>`.");
        ImGui::End();
        return;
    }

    ImGui::Text("%s  |  %s", RoleLabel(session->Role()),
                NetAddressToString(session->LocalAddress()).c_str());
    if (session->Role() == NetSessionRole::Client)
    {
        ImGui::Text("peer %u  |  rtt %.1f ms", session->LocalPeerId().Value,
                    static_cast<double>(session->RoundTripMicroseconds()) / 1000.0);
        if (Clock.HasEstimate())
        {
            // Local tick, what it is called on the authority, and how far
            // ahead this machine is stamping the input it sends.
            const std::uint64_t local = session->LocalTick();
            ImGui::Text("tick %" PRIu64 " -> authority %" PRIu64
                        "  |  sending for %" PRIu64 " (+%d)",
                        local, Clock.AuthorityTickAt(local),
                        Clock.CommandTickAt(local),
                        static_cast<int>(Clock.CommandOffset() - Clock.Offset()));
            ImGui::SetItemTooltip(
                "Flight time plus slack. Input stamped for a tick the "
                "authority has already run is input thrown away.");
        }
        else
        {
            ImGui::TextUnformatted("authority clock not named yet");
        }
    }
    else
    {
        ImGui::Text("tick %" PRIu64, session->LocalTick());
    }

    ImGui::SeparatorText("Traffic");
    RateText("in", Traffic.TotalIn());
    RateText("out", Traffic.TotalOut());
    PlotBytes("B/s in", Traffic.BytesInHistory());
    PlotBytes("B/s out", Traffic.BytesOutHistory());

    if (ImGui::TreeNode("By kind"))
    {
        for (std::size_t index = 0; index < kNetTrafficKinds; ++index)
        {
            const auto kind = static_cast<NetTrafficKind>(index);
            const std::string_view name = NetTrafficKindToString(kind);
            ImGui::Text("%.*s", static_cast<int>(name.size()), name.data());
            ImGui::Indent();
            RateText("in", Traffic.In(kind));
            RateText("out", Traffic.Out(kind));
            ImGui::Unindent();
        }
        ImGui::TreePop();
    }

    if (session->Role() == NetSessionRole::Client)
    {
        ImGui::SeparatorText("Prediction");
        if (!Prediction.Predicted().IsValid())
        {
            ImGui::TextUnformatted("no pawn yet");
        }
        else if (!Prediction.IsEnabled())
        {
            ImGui::TextUnformatted(
                "off (net.prediction) -- input costs a round trip");
        }
        else
        {
            // Reset distance is what says whether this is working. A steady
            // few centimetres is healthy; a rising floor is the two simulations
            // drifting apart faster than they are pulled together.
            ImGui::Text("reset %.3f m  |  %u ticks replayed  |  %" PRIu64
                        " reconciles, %" PRIu64 " snaps",
                        static_cast<double>(Prediction.LastResetMeters()),
                        Prediction.LastReplayedTicks(), Prediction.Reconciles(),
                        Prediction.Snaps());
            ImGui::SetItemTooltip(
                "How far the pawn moved when the authority's state replaced "
                "this machine's guess and the unanswered ticks were re-run. "
                "Snaps are stalls too long to replay through.");
        }

        ImGui::SeparatorText("Interpolation");
        if (!Interpolation.IsEnabled())
        {
            ImGui::TextUnformatted(
                "off (net.interpolation) -- others step as snapshots land");
        }
        else if (Interpolation.TrackedCount() == 0)
        {
            ImGui::TextUnformatted("nothing mirrored yet");
        }
        else
        {
            // Held ticks are the tell. A few are ordinary; a rising share means
            // the presented tick is running past the newest sample, which is
            // the delay being too short for this connection rather than
            // anything going wrong here.
            ImGui::Text("%zu mirrored, %u tick delay  |  %" PRIu64
                        " blended, %" PRIu64 " held of %" PRIu64,
                        Interpolation.TrackedCount(), Interpolation.DelayTicks(),
                        Interpolation.Interpolated(), Interpolation.Held(),
                        Interpolation.Resolved());
            ImGui::SetItemTooltip(
                "Poses resolved between two snapshots, against poses that ran "
                "past the newest one and stopped. Raise net.interp_delay_ticks "
                "if held is climbing.");
        }
    }

    if (session->Role() == NetSessionRole::Host)
    {
        ImGui::SeparatorText("Replication");
        const ReplicationRuntime::PublishStats& publish = Replication.LastPublish();
        if (publish.SnapshotsSent == 0)
        {
            ImGui::TextUnformatted("nothing published yet");
        }
        else
        {
            const double occupancy =
                publish.BudgetBytes == 0
                    ? 0.0
                    : 100.0 * static_cast<double>(publish.PeakSnapshotBytes)
                          / static_cast<double>(publish.BudgetBytes);
            ImGui::Text("%u snapshots  |  %zu of %zu B (%.0f%%)",
                        publish.SnapshotsSent, publish.PeakSnapshotBytes,
                        publish.BudgetBytes, occupancy);
            ImGui::SetItemTooltip(
                "The fullest snapshot of the last publish, against the budget "
                "one may occupy. Entities deferred while this sits well under "
                "the budget means something other than bytes is limiting.");

            // Deferral is the budget working. Age is what says whether the
            // queue is draining: a number that keeps climbing is an entity
            // nobody is getting to.
            ImGui::Text("%u deferred, oldest %u snapshots behind",
                        publish.EntitiesDeferred, publish.OldestDeferredSnapshots);
            ImGui::SetItemTooltip(
                "Entities that had something to say and were left for a later "
                "snapshot. They keep their place; nothing is lost by waiting.");

            if (publish.EntitiesUnsendable > 0)
            {
                // Not back-pressure. Nothing about these entities can reach a
                // peer at any budget they would fit in, so waiting will not
                // help and only a smaller entity or a larger budget will.
                ImGui::TextColored(
                    ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                    "%u entities too large for a snapshot -- raise "
                    "net.snapshot_bytes", publish.EntitiesUnsendable);
            }
        }
    }

    ImGui::SeparatorText("Peers");
    const std::vector<PeerId> peers = session->ConnectedPeers();
    if (peers.empty())
    {
        ImGui::TextUnformatted("none connected");
    }
    else if (ImGui::BeginTable("peers", 5,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("peer");
        ImGui::TableSetupColumn("rtt");
        ImGui::TableSetupColumn("strikes");
        ImGui::TableSetupColumn("queued");
        ImGui::TableSetupColumn("starved");
        ImGui::TableHeadersRow();

        for (PeerId peer : peers)
        {
            const NetPeer* record = session->FindPeer(peer);
            const NetPeerCommandBuffer* input = Commands.Peer(peer);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u", peer.Value);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f ms",
                        record == nullptr
                            ? 0.0
                            : static_cast<double>(record->RoundTripMicroseconds) / 1000.0);
            ImGui::TableNextColumn();
            ImGui::Text("%u", record == nullptr ? 0u : record->Strikes);
            ImGui::TableNextColumn();
            // Ticks of input held back before this peer is simulated. Every one
            // of them is latency the player pays on every input.
            ImGui::Text("%zu",
                        input == nullptr ? std::size_t{ 0 } : input->QueuedTicks());
            ImGui::TableNextColumn();
            ImGui::Text("%" PRIu64,
                        input == nullptr ? std::uint64_t{ 0 }
                                         : input->StarvedTicks());
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Tuning");
    // Written through the console rather than onto the runtime, so the value a
    // session enforces and the value shown here cannot disagree.
    int slack = static_cast<int>(Commands.TargetDepth());
    if (ImGui::SliderInt("command slack (ticks)", &slack, 0, 8))
    {
        (void)Console.SetCVar("net.command_slack",
                              static_cast<std::int64_t>(slack),
                              ConsoleValueSource{ "net stats panel" },
                              ConsolePhase::EngineReady);
    }
    ImGui::SetItemTooltip(
        "Ticks of input the authority holds back before simulating a peer. "
        "Higher rides out a jittery connection; every tick is added latency.");

    if (session->Role() == NetSessionRole::Host)
    {
        int budget = static_cast<int>(Replication.SnapshotBytes());
        if (ImGui::SliderInt("snapshot budget (B)", &budget,
                             static_cast<int>(kNetMinSnapshotBytes),
                             static_cast<int>(kNetMaxSnapshotBytes)))
        {
            (void)Console.SetCVar("net.snapshot_bytes",
                                  static_cast<std::int64_t>(budget),
                                  ConsoleValueSource{ "net stats panel" },
                                  ConsolePhase::EngineReady);
        }
        ImGui::SetItemTooltip(
            "Bytes one snapshot may occupy, per peer. Lowering it costs "
            "convergence speed, not correctness: what does not fit rides the "
            "next snapshot.");
    }

    ImGui::Text("session totals: %" PRIu64 " B in, %" PRIu64 " B out, "
                "%" PRIu64 " strikes, %" PRIu64 " refusals",
                Traffic.LifetimeBytesIn(), Traffic.LifetimeBytesOut(),
                session->StrikesIssued(), session->Refusals());

    ImGui::End();
}
