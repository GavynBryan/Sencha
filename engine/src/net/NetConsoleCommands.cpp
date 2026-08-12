#include <net/NetConsoleCommands.h>

#include <app/Engine.h>
#include <app/GameModuleAbi.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleTypes.h>
#include <net/NetSession.h>
#include <net/NetStatusReport.h>
#include <net/ReplicationLayout.h>
#include <world/RuntimeWorld.h>
#include <net/ClientPrediction.h>
#include <net/PeerCommandRuntime.h>
#include <net/UdpTransport.h>

#include <algorithm>
#include <charconv>
#include <limits>
#include <memory>
#include <string>
#include <variant>

namespace
{
    // The transport the console-started sessions run on. Owned here rather than
    // by the session so the session keeps taking a reference and stays testable
    // against a loopback pair; this is the composition root for the real one.
    std::unique_ptr<UdpTransport> ConsoleTransport;

    constexpr std::uint16_t kDefaultPort = 27500;

    // What both ends compare at the handshake. Assembled from the running build
    // rather than from anything typed, so a player cannot argue their way past
    // the gate by passing different arguments.
    NetIdentity IdentityFor(const Engine& engine)
    {
        NetIdentity identity;
        identity.ModuleFingerprint = SenchaThisBuildAbi().HeaderFingerprint;
        // The compiled replicated table, which the ABI fingerprint cannot stand
        // in for: a game module deciding a different set of its components
        // replicates changes no engine header, and the two builds would then
        // read each other's snapshots as different components.
        identity.ReplicationTableHash = engine.ReplicatedComponents().TableHash();
        // Content identity is the world cook's to supply and does not exist yet,
        // so it is zero on both ends today: matching, and therefore inert. It
        // becomes load-bearing when the world hash lands, and the gate is
        // already reading it so nothing has to be rewired then.
        identity.WorldIdentity = 0;
        identity.FixedTickRateMilliHz = static_cast<std::uint32_t>(
            engine.Config().Runtime.FixedTickRate * 1000.0);
        return identity;
    }

    bool ParseUnsigned(std::string_view text, std::uint64_t& out)
    {
        const auto result =
            std::from_chars(text.data(), text.data() + text.size(), out);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    bool ParsePort(std::string_view text, std::uint16_t& out)
    {
        unsigned value = 0;
        const auto result =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
            return false;
        if (value > 65535)
            return false;
        out = static_cast<std::uint16_t>(value);
        return true;
    }

}

void NetApplyConsoleAuthority(ConsoleRegistry& registry, const NetSession* session)
{
    ConsoleAuthorityPolicy policy;
    if (session != nullptr && session->Role() != NetSessionRole::Standalone)
    {
        policy.InSession = true;
        // A host holds authority the moment it binds; a client only once it is
        // admitted, so a half-open connection does not silently unlock the
        // console.
        policy.HasAuthority = session->Role() == NetSessionRole::Host;
        if (const CVarMetadata* cheats = registry.FindCVar("net.cheats"))
        {
            if (const bool* enabled = std::get_if<bool>(&cheats->CurrentValue))
                policy.CheatsEnabled = *enabled;
        }
    }
    registry.SetAuthorityPolicy(policy);
}

namespace
{
    // Pushes registered defaults into the runtime state that holds them. Only
    // needed for cvars whose value lives somewhere other than the registry;
    // reading a cvar every frame would not need this.
    void ApplyNetCVarDefaults(ConsoleRegistry& registry, Engine& engine)
    {
        if (const CVarMetadata* interval = registry.FindCVar("net.snapshot_interval"))
        {
            if (const std::int64_t* ticks =
                    std::get_if<std::int64_t>(&interval->CurrentValue))
            {
                const auto value =
                    static_cast<std::uint32_t>(std::max<std::int64_t>(1, *ticks));
                engine.Replication().SetPublishInterval(value);
                engine.Interpolation().SetSnapshotInterval(value);
            }
        }
        if (const CVarMetadata* budget = registry.FindCVar("net.snapshot_bytes"))
        {
            if (const std::int64_t* bytes =
                    std::get_if<std::int64_t>(&budget->CurrentValue))
            {
                engine.Replication().SetSnapshotBytes(
                    static_cast<std::size_t>(std::max<std::int64_t>(0, *bytes)));
            }
        }
    }
}

void RegisterNetConsoleCommands(ConsoleRegistry& registry, Engine& engine)
{
    registry.RegisterCVar({
        .Name = "net.cheats",
        .Owner = "engine",
        .Type = CVarType::Bool,
        .DefaultValue = false,
        .CurrentValue = false,
        // Replicated so only the authority decides it, and Transient so a
        // server that once enabled cheats does not quietly start that way
        // again next launch.
        .Flags = CVarFlags::Transient | CVarFlags::Replicated,
        .Help = "Whether Cheat-flagged cvars may be changed during a session. "
                "Off blocks them for everyone including the host.",
        .Source = { "engine defaults" },
    });

    registry.RegisterCVar({
        .Name = "net.max_peers",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(4),
        .CurrentValue = static_cast<std::int64_t>(4),
        .Flags = CVarFlags::Archive,
        .Help = "Peers a hosted session admits. Four is the co-op shape and the "
                "default; sixteen is measured and is what the arena modes need.",
        .Source = { "engine defaults" },
        .Min = static_cast<std::int64_t>(1),
        .Max = static_cast<std::int64_t>(kNetMaxPeersSupported),
    });

    registry.RegisterCVar({
        .Name = "net.command_slack",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(
            NetPeerCommandBuffer::kTargetDepth),
        .CurrentValue = static_cast<std::int64_t>(
            NetPeerCommandBuffer::kTargetDepth),
        // The authority's to set: it is the machine doing the buffering, and a
        // client choosing how late it is simulated would be choosing its own
        // advantage.
        .Flags = CVarFlags::Archive | CVarFlags::Replicated,
        .Help = "Ticks of input the authority holds back before simulating a "
                "peer. Higher rides out a jittery connection; every tick is "
                "latency that peer pays on every input.",
        .Source = { "engine defaults" },
        .Min = static_cast<std::int64_t>(0),
        .Max = static_cast<std::int64_t>(NetPeerCommandBuffer::kCapacity - 1),
        .OnChange = [&engine](const CVarChangeContext& change) {
            if (const std::int64_t* ticks =
                    std::get_if<std::int64_t>(&change.NewValue))
            {
                engine.PeerCommands().SetTargetDepth(
                    static_cast<std::size_t>(std::max<std::int64_t>(0, *ticks)));
            }
        },
    });

    registry.RegisterCVar({
        .Name = "net.snapshot_interval",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(kNetDefaultSnapshotInterval),
        .CurrentValue = static_cast<std::int64_t>(kNetDefaultSnapshotInterval),
        // The authority's, like the command slack: it is paying the per-peer
        // cost, and a client choosing how often it is told about the world
        // would be choosing how much of the session's bandwidth it takes.
        // Clients are told so they can hold mirrored entities the right
        // distance behind rather than guessing at the cadence.
        .Flags = CVarFlags::Archive | CVarFlags::Replicated,
        .Help = "Simulation ticks between snapshots. One is every tick; higher "
                "trades freshness for bandwidth, which is what decides how "
                "many players fit in a session.",
        .Source = { "engine defaults" },
        .Min = static_cast<std::int64_t>(1),
        // Past this the interpolation buffer cannot bracket a presented tick
        // from the samples it keeps, and mirrored motion would hold rather
        // than blend.
        .Max = static_cast<std::int64_t>(ReplicationInterpolation::kSamples / 2),
        .OnChange = [&engine](const CVarChangeContext& change) {
            if (const std::int64_t* ticks =
                    std::get_if<std::int64_t>(&change.NewValue))
            {
                const auto interval =
                    static_cast<std::uint32_t>(std::max<std::int64_t>(1, *ticks));
                engine.Replication().SetPublishInterval(interval);
                // An authority mirrors other players too, so it holds the same
                // presentation lag its clients do.
                engine.Interpolation().SetSnapshotInterval(interval);
            }
        },
    });

    registry.RegisterCVar({
        .Name = "net.snapshot_bytes",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(kNetMaxSnapshotBytes),
        .CurrentValue = static_cast<std::int64_t>(kNetMaxSnapshotBytes),
        // The authority's: it is the machine paying this per peer, and it is
        // the one number that decides how much of a link a full session takes.
        // Not replicated, because nothing on a client behaves differently for
        // knowing it -- a snapshot describes itself.
        .Flags = CVarFlags::Archive,
        .Help = "Bytes one snapshot may occupy, per peer. Lowering it loses "
                "nothing: what does not fit is carried by a later snapshot. It "
                "trades how fast a peer converges on the world for how much of "
                "the link the authority takes to get it there.",
        .Source = { "engine defaults" },
        .Min = static_cast<std::int64_t>(kNetMinSnapshotBytes),
        // A snapshot has to fit one datagram; there is no fragmentation behind
        // it and no resend, so a larger budget would simply not arrive.
        .Max = static_cast<std::int64_t>(kNetMaxSnapshotBytes),
        .OnChange = [&engine](const CVarChangeContext& change) {
            if (const std::int64_t* bytes =
                    std::get_if<std::int64_t>(&change.NewValue))
            {
                engine.Replication().SetSnapshotBytes(
                    static_cast<std::size_t>(std::max<std::int64_t>(0, *bytes)));
            }
        },
    });

    registry.RegisterCVar({
        .Name = "net.prediction",
        .Owner = "engine",
        .Type = CVarType::Bool,
        .DefaultValue = true,
        .CurrentValue = true,
        // Local to each client. It changes only what this machine draws for
        // itself between snapshots; the authority simulates the same either
        // way, so nobody gains anything by turning it off but their own feel.
        .Flags = CVarFlags::Archive,
        .Help = "Simulate this client's own pawn immediately instead of waiting "
                "for the authority. Off costs a round trip on every input.",
        .Source = { "engine defaults" },
        .OnChange = [&engine](const CVarChangeContext& change) {
            if (const bool* on = std::get_if<bool>(&change.NewValue))
                engine.Prediction().SetEnabled(*on);
        },
    });

    registry.RegisterCVar({
        .Name = "net.interpolation",
        .Owner = "engine",
        .Type = CVarType::Bool,
        .DefaultValue = true,
        .CurrentValue = true,
        // Local to each client, for the same reason prediction is: it changes
        // what this machine draws between snapshots and nothing the authority
        // simulates.
        .Flags = CVarFlags::Archive,
        .Help = "Draw mirrored entities along the authority's path at a small "
                "delay. Off steps them whenever a snapshot lands, which is "
                "visible as stutter on any connection with jitter.",
        .Source = { "engine defaults" },
        .OnChange = [&engine](const CVarChangeContext& change) {
            if (const bool* on = std::get_if<bool>(&change.NewValue))
                engine.Interpolation().SetEnabled(*on);
        },
    });

    registry.RegisterCVar({
        .Name = "net.interp_delay_ticks",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(
            ReplicationInterpolation::kDefaultDelayTicks),
        .CurrentValue = static_cast<std::int64_t>(
            ReplicationInterpolation::kDefaultDelayTicks),
        .Flags = CVarFlags::Archive,
        .Help = "Jitter margin, in ticks, for drawing mirrored entities -- on "
                "top of measured flight time and the authority's snapshot "
                "interval, both of which are accounted for already. Higher "
                "rides out more jitter; every tick is how far in the past "
                "other players are.",
        .Source = { "engine defaults" },
        .Min = static_cast<std::int64_t>(0),
        // Margin only. The interval this rides on top of is bounded separately,
        // and the two together stay inside the sample window by construction.
        .Max = static_cast<std::int64_t>(ReplicationInterpolation::kSamples - 1),
        .OnChange = [&engine](const CVarChangeContext& change) {
            if (const std::int64_t* ticks = std::get_if<std::int64_t>(&change.NewValue))
            {
                engine.Interpolation().SetDelayTicks(
                    static_cast<std::uint32_t>(std::max<std::int64_t>(0, *ticks)));
            }
        },
    });

    registry.RegisterCommand({
        .Name = "host",
        .Owner = "engine",
        .Usage = "host [port]",
        .Help = "Start hosting a session on this machine. Default port 27500.",
        .Callback = [&engine, &registry](ConsoleExecutionContext&,
                              std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() > 1)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("usage: host [port]");
                return result;
            }
            if (engine.TryNet() != nullptr)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("already in a session; disconnect first");
                return result;
            }

            std::uint16_t port = kDefaultPort;
            if (!args.empty() && !ParsePort(args[0], port))
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("port must be 0-65535");
                return result;
            }

            ConsoleTransport = std::make_unique<UdpTransport>();
            NetSession* session = engine.CreateNetSession(*ConsoleTransport);
            if (session == nullptr || !session->Host(port, IdentityFor(engine)))
            {
                engine.DestroyNetSession();
                ConsoleTransport.reset();
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("could not bind port " + std::to_string(port));
                return result;
            }

            if (const CVarMetadata* cap = registry.FindCVar("net.max_peers"))
            {
                if (const auto* value = std::get_if<std::int64_t>(&cap->CurrentValue))
                    session->SetMaxPeers(static_cast<std::size_t>(*value));
            }

            result.Info("hosting on " + NetAddressToString(session->LocalAddress()));
            return result;
        },
    });

    registry.RegisterCommand({
        .Name = "connect",
        .Owner = "engine",
        .Usage = "connect <address[:port]>",
        .Help = "Join a session. Port defaults to 27500 when omitted.",
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("usage: connect <address[:port]>");
                return result;
            }
            if (engine.TryNet() != nullptr)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("already in a session; disconnect first");
                return result;
            }

            std::string text = args[0];
            // A bare address is the common case when everyone is on the default
            // port, so it is accepted rather than rejected on a technicality.
            if (text.find(':') == std::string::npos)
                text += ":" + std::to_string(kDefaultPort);

            const std::optional<NetAddress> address = NetAddressFromString(text);
            if (!address.has_value())
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("could not parse address '" + args[0] + "'");
                return result;
            }

            ConsoleTransport = std::make_unique<UdpTransport>();
            NetSession* session = engine.CreateNetSession(*ConsoleTransport);
            if (session == nullptr || !session->Connect(*address, IdentityFor(engine)))
            {
                engine.DestroyNetSession();
                ConsoleTransport.reset();
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("could not open a socket to " + text);
                return result;
            }

            // Admission is several round trips away; the status command is how
            // the outcome is read, because a console command cannot block a
            // frame waiting for a peer.
            result.Info("connecting to " + NetAddressToString(*address)
                        + "; use net_status");
            return result;
        },
    });

    registry.RegisterCommand({
        .Name = "disconnect",
        .Owner = "engine",
        .Usage = "disconnect [reason]",
        .Help = "Leave or end the current session.",
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string> args) {
            ConsoleResult result;
            NetSession* session = engine.TryNet();
            if (session == nullptr)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("not in a session");
                return result;
            }

            std::string reason = args.empty() ? std::string("disconnected") : args[0];
            session->Disconnect(reason);
            engine.DestroyNetSession();
            ConsoleTransport.reset();
            result.Info("disconnected");
            return result;
        },
    });

    registry.RegisterCommand({
        .Name = "net_entity",
        .Owner = "engine",
        .Usage = "net_entity <netid> [peer]",
        .Help = "Print one replicated object's record: owner, when each field "
                "run last moved, and how far each peer has got with it. Naming "
                "a peer reports what that peer is still owed, by name.",
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string> args) {
            ConsoleResult result;
            std::uint64_t id = 0;
            if (args.empty() || !ParseUnsigned(args[0], id))
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("usage: net_entity <netid> [peer]");
                return result;
            }

            PeerId focus;
            if (args.size() > 1)
            {
                std::uint64_t peer = 0;
                if (!ParseUnsigned(args[1], peer) || peer == 0
                    || peer > std::numeric_limits<std::uint32_t>::max())
                {
                    result.Status = ConsoleStatus::InvalidArguments;
                    result.Error("peer ids start at 1");
                    return result;
                }
                focus = PeerId{ static_cast<std::uint32_t>(peer) };
            }

            NetEntityReportSources sources;
            sources.Session = engine.TryNet();
            sources.Replication = &engine.Replication();
            sources.Layout = &engine.ReplicatedComponents();
            result.Info(NetFormatEntity(sources, NetEntityId{ id }, focus));
            return result;
        },
    });

    registry.RegisterCommand({
        .Name = "net_owners",
        .Owner = "engine",
        .Usage = "net_owners",
        .Help = "Print what this machine drives and, on a host, what each peer "
                "owns -- straight off the NetOwner column, which is the only "
                "record of it.",
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string>) {
            ConsoleResult result;
            result.Info(NetFormatOwners(engine.TryNet(),
                                        engine.World().Entities(),
                                        &engine.Replication()));
            return result;
        },
    });

    registry.RegisterCommand({
        .Name = "net_components",
        .Owner = "engine",
        .Usage = "net_components",
        .Help = "Print the sealed replicated component table: wire key, name, "
                "and each field's visibility.",
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string>) {
            // The table is compiled from what components declare, so there is
            // no list to read to find out what replicates. This prints what the
            // build actually sealed, which is the only answer that is never
            // out of date.
            ConsoleResult result;
            const ReplicationLayout& layout = engine.ReplicatedComponents();

            std::string text = "replicated components: "
                             + std::to_string(layout.Size())
                             + "  (table hash "
                             + std::to_string(layout.TableHash()) + ")";
            for (std::size_t index = 0; index < layout.Size(); ++index)
            {
                const ReplicatedComponent* component =
                    layout.At(static_cast<std::uint8_t>(index));
                if (component == nullptr)
                    continue;

                text += "\n  " + std::to_string(index) + "  "
                      + std::string(component->Name);
                for (const ReplicatedField& field : component->Fields)
                {
                    text += "\n      " + field.Name;
                    if (field.OwnerOnly)
                        text += "  owner-only";
                    if (field.OwnerLocal)
                        text += "  everyone-but-owner";
                    if (field.Quantization.IsQuantized())
                    {
                        text += "  quantized " + std::to_string(field.Quantization.Bits)
                              + " bits";
                    }
                }
            }
            result.Info(text);
            return result;
        },
    });

    registry.RegisterCommand({
        .Name = "net_status",
        .Owner = "engine",
        .Usage = "net_status",
        .Help = "Print what the session is doing: traffic by kind, replication "
                "budget and deferral, per-peer input depth and channel health, "
                "and a client's prediction, interpolation, and clock.",
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string>) {
            // The whole account, not a summary of it. A dedicated host has no
            // overlay, so this is the only place the numbers that decide
            // whether a session is healthy can be read at all.
            NetStatusSources sources;
            sources.Session = engine.TryNet();
            sources.Traffic = &engine.NetTraffic();
            sources.Replication = &engine.Replication();
            sources.Commands = &engine.PeerCommands();
            sources.Prediction = &engine.Prediction();
            sources.Interpolation = &engine.Interpolation();
            sources.Clock = &engine.NetClock();

            ConsoleResult result;
            result.Info(NetFormatStatus(sources));
            return result;
        },
    });

    // Cvars whose value is state somewhere else are applied once here, because
    // OnChange fires on change: a default that never changes would never be
    // delivered, and the engine would run at a cadence nobody chose until
    // someone happened to set it back to what it already said.
    ApplyNetCVarDefaults(registry, engine);
}
