#include <debug/NetStatsPanel.h>

#include <core/console/ConsoleRegistry.h>
#include <net/ClientPrediction.h>
#include <net/NetSession.h>
#include <net/NetStats.h>
#include <net/NetTickEstimator.h>
#include <net/PeerCommandRuntime.h>

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
                             PeerCommandRuntime& commands,
                             ConsoleRegistry& console)
    : Session(session)
    , Traffic(traffic)
    , Clock(clock)
    , Prediction(prediction)
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
            ImGui::TextUnformatted("off (net.prediction) -- input costs a round trip");
        }
        else
        {
            // Error is what says whether this is working. A floor that creeps
            // up is prediction drifting away from the authority; snaps are it
            // giving up and moving the player.
            ImGui::Text("error %.3f m  |  %" PRIu64 " corrections, %" PRIu64
                        " snaps of %" PRIu64 " checks",
                        static_cast<double>(Prediction.LastErrorMeters()),
                        Prediction.Corrections(), Prediction.Snaps(),
                        Prediction.Observations());
            ImGui::SetItemTooltip(
                "Distance between where this machine put the pawn and where the "
                "authority says it was, at the same tick.");
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

    ImGui::Text("session totals: %" PRIu64 " B in, %" PRIu64 " B out, "
                "%" PRIu64 " strikes, %" PRIu64 " refusals",
                Traffic.LifetimeBytesIn(), Traffic.LifetimeBytesOut(),
                session->StrikesIssued(), session->Refusals());

    ImGui::End();
}
