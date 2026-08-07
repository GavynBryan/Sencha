#include <net/NetConsoleCommands.h>

#include <app/Engine.h>
#include <app/GameModuleAbi.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleTypes.h>
#include <net/NetSession.h>
#include <net/UdpTransport.h>

#include <charconv>
#include <memory>
#include <string>

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
        // Content identity is the world cook's to supply and does not exist yet,
        // so it is zero on both ends today: matching, and therefore inert. It
        // becomes load-bearing when the world hash lands, and the gate is
        // already reading it so nothing has to be rewired then.
        identity.WorldIdentity = 0;
        identity.FixedTickRateMilliHz = static_cast<std::uint32_t>(
            engine.Config().Runtime.FixedTickRate * 1000.0);
        return identity;
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

    std::string_view DescribeRole(NetSessionRole role)
    {
        switch (role)
        {
        case NetSessionRole::Host:   return "host";
        case NetSessionRole::Client: return "client";
        case NetSessionRole::Standalone: break;
        }
        return "standalone";
    }

    std::string_view DescribeFailure(NetJoinFailure failure)
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

    std::string DescribeRoundTrip(std::uint64_t microseconds)
    {
        return std::to_string(microseconds / 1000) + "."
             + std::to_string((microseconds / 100) % 10) + "ms";
    }
}

void RegisterNetConsoleCommands(ConsoleRegistry& registry, Engine& engine)
{
    registry.RegisterCommand({
        .Name = "host",
        .Owner = "engine",
        .Usage = "host [port]",
        .Help = "Start hosting a session on this machine. Default port 27500.",
        .Callback = [&engine](ConsoleExecutionContext&,
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
        .Name = "net_status",
        .Owner = "engine",
        .Usage = "net_status",
        .Help = "Print session role, local address, peers, and handshake outcome.",
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string>) {
            ConsoleResult result;
            const NetSession* session = engine.TryNet();
            if (session == nullptr)
            {
                result.Info("standalone (no session)");
                return result;
            }

            std::string text = std::string(DescribeRole(session->Role()));
            text += " at " + NetAddressToString(session->LocalAddress());

            if (session->Role() == NetSessionRole::Client)
            {
                text += session->IsConnected() ? "; admitted as peer "
                                                   + std::to_string(session->LocalPeerId().Value)
                                               : "; not admitted";
                if (session->RoundTripMicroseconds() > 0)
                    text += "; rtt " + DescribeRoundTrip(session->RoundTripMicroseconds());
            }
            else
            {
                text += "; peers " + std::to_string(session->ConnectedPeers().size());
                for (PeerId id : session->ConnectedPeers())
                {
                    const NetPeer* peer = session->FindPeer(id);
                    if (peer != nullptr && peer->RoundTripMicroseconds > 0)
                    {
                        text += "; peer " + std::to_string(id.Value) + " rtt "
                              + DescribeRoundTrip(peer->RoundTripMicroseconds);
                    }
                }
            }
            if (session->JoinFailure() != NetJoinFailure::None)
            {
                text += "; " + std::string(DescribeFailure(session->JoinFailure()))
                      + ": " + session->JoinFailureReason();
            }

            text += "; strikes " + std::to_string(session->StrikesIssued());
            text += "; refusals " + std::to_string(session->Refusals());
            result.Info(text);
            return result;
        },
    });
}
