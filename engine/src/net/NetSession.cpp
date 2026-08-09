#include <net/NetSession.h>

#include <algorithm>
#include <array>

namespace
{
    // One datagram of scratch, reused for every encode. Nothing here allocates
    // per message in steady state.
    using Scratch = std::array<std::byte, kNetMaxDatagramBytes>;

    std::string_view DescribeChannel(NetChannelKind kind)
    {
        return kind == NetChannelKind::ReliableOrdered ? "reliable" : "unreliable";
    }

    std::uint64_t MicroFromSeconds(double seconds)
    {
        return static_cast<std::uint64_t>(seconds * 1'000'000.0);
    }

    // The admitted-peer paths peek the leading byte to route control messages
    // before channel delivery. That is only unambiguous while no control type
    // shares a value with a channel kind's leading byte.
    constexpr auto kHighestChannelKind =
        static_cast<std::uint8_t>(NetChannelKind::ReliableOrdered);
    static_assert(static_cast<std::uint8_t>(NetMessageType::Disconnect) > kHighestChannelKind);
    static_assert(static_cast<std::uint8_t>(NetMessageType::Ping) > kHighestChannelKind);
    static_assert(static_cast<std::uint8_t>(NetMessageType::Pong) > kHighestChannelKind);
}

std::string_view NetDescribeIdentityMismatch(const NetIdentity& local,
                                             const NetIdentity& remote)
{
    // Named field by field: "incompatible" tells a player nothing, and the
    // three causes have completely different fixes.
    if (local.ModuleFingerprint != remote.ModuleFingerprint)
        return "game build mismatch";
    if (local.ReplicationTableHash != remote.ReplicationTableHash)
        return "replicated component mismatch";
    if (local.WorldIdentity != remote.WorldIdentity)
        return "world content mismatch";
    if (local.FixedTickRateMilliHz != remote.FixedTickRateMilliHz)
        return "simulation tick rate mismatch";
    return {};
}

NetSession::NetSession(INetTransport& transport)
    : Transport(transport)
{
}

std::vector<std::byte> NetSession::CookieFor(const NetAddress& address) const
{
    // Folds the address and the session secret together. Not a keyed MAC from a
    // crypto library -- that arrives with the AEAD -- but it is bound to the
    // address and unpredictable without the secret, which is the property the
    // handshake actually needs: an attacker forging a source address never sees
    // the challenge sent to that address, so it cannot echo a valid cookie.
    std::uint64_t folded = Secret;
    const auto mix = [&folded](std::uint8_t byte) {
        folded ^= byte;
        folded *= 1099511628211ull;
    };
    for (std::uint8_t byte : address.Bytes)
        mix(byte);
    mix(static_cast<std::uint8_t>(address.Port & 0xFF));
    mix(static_cast<std::uint8_t>(address.Port >> 8));
    mix(static_cast<std::uint8_t>(address.Family));

    std::vector<std::byte> cookie(8);
    for (std::size_t i = 0; i < cookie.size(); ++i)
        cookie[i] = static_cast<std::byte>((folded >> (i * 8)) & 0xFF);
    return cookie;
}

bool NetSession::Host(std::uint16_t port, const NetIdentity& identity)
{
    if (CurrentRole != NetSessionRole::Standalone)
        return false;
    if (!Transport.Open(port))
        return false;

    CurrentRole = NetSessionRole::Host;
    LocalIdentity = identity;
    Local = Transport.LocalAddress();
    // Derived from the address the socket actually got, so two hosts on one
    // machine do not issue interchangeable cookies.
    Secret = 1469598103934665603ull
           ^ (static_cast<std::uint64_t>(Local.Port) << 32)
           ^ static_cast<std::uint64_t>(Local.Bytes[3]);
    return true;
}

bool NetSession::Connect(const NetAddress& authority, const NetIdentity& identity)
{
    if (CurrentRole != NetSessionRole::Standalone)
        return false;
    if (!authority.IsValid())
        return false;
    if (!Transport.Open(0))
        return false;

    CurrentRole = NetSessionRole::Client;
    LocalIdentity = identity;
    Local = Transport.LocalAddress();
    AuthorityAddress = authority;
    AwaitingChallenge = true;
    Admitted = false;
    ConnectClockArmed = false;
    ConnectStartedSeconds = 0.0;
    AuthorityLastHeardSeconds = 0.0;
    LastPingSentSeconds = 0.0;
    RttMicroseconds = 0;
    Failure = NetJoinFailure::None;
    FailureReason.clear();

    SendHello();
    return true;
}

void NetSession::SendHello()
{
    // Re-arms the challenge gate: a retry is asking to be challenged again, and
    // the answer to the previous ask may simply have been lost.
    AwaitingChallenge = true;

    Scratch scratch{};
    const NetHello hello{ .ProtocolVersion = kNetProtocolVersion };
    SendRaw(AuthorityAddress, NetEncodeHello(hello, scratch));
}

void NetSession::Disconnect(std::string_view reason)
{
    if (CurrentRole == NetSessionRole::Standalone)
        return;

    Scratch scratch{};
    NetDisconnect message;
    message.Reason = std::string(reason.substr(
        0, std::min(reason.size(), NetDefaultCaps().MaxReasonBytes)));
    const auto encoded = NetEncodeDisconnect(message, scratch);

    if (CurrentRole == NetSessionRole::Client)
    {
        SendRaw(AuthorityAddress, encoded);
    }
    else
    {
        for (const NetPeer& peer : Peers)
        {
            if (peer.State == NetPeerState::Connected)
                SendRaw(peer.Address, encoded);
        }
    }

    Peers.clear();
    ClientChannels = NetChannelSet{};
    CurrentRole = NetSessionRole::Standalone;
    Admitted = false;
    AwaitingChallenge = false;
    SelfId = PeerId{};
    RttMicroseconds = 0;
    Transport.Close();
}

bool NetSession::IsConnected() const
{
    if (CurrentRole == NetSessionRole::Client)
        return Admitted;
    if (CurrentRole == NetSessionRole::Host)
        return true;
    return false;
}

NetPeer* NetSession::PeerByAddress(const NetAddress& address)
{
    for (NetPeer& peer : Peers)
    {
        if (peer.Address == address)
            return &peer;
    }
    return nullptr;
}

const NetPeer* NetSession::FindPeer(PeerId id) const
{
    for (const NetPeer& peer : Peers)
    {
        if (peer.Id == id)
            return &peer;
    }
    return nullptr;
}

std::vector<PeerId> NetSession::ConnectedPeers() const
{
    std::vector<PeerId> out;
    out.reserve(Peers.size());
    for (const NetPeer& peer : Peers)
    {
        if (peer.State == NetPeerState::Connected)
            out.push_back(peer.Id);
    }
    return out;
}

void NetSession::SendRaw(const NetAddress& to, std::span<const std::byte> bytes)
{
    if (!bytes.empty())
        (void)Transport.Send(to, bytes);
}

void NetSession::RefuseAt(const NetAddress& address, std::string_view reason)
{
    ++TotalRefusals;
    Scratch scratch{};
    NetRefuse refuse;
    refuse.Reason = std::string(reason.substr(
        0, std::min(reason.size(), NetDefaultCaps().MaxReasonBytes)));
    SendRaw(address, NetEncodeRefuse(refuse, scratch));
}

void NetSession::Strike(NetPeer& peer, std::string_view why)
{
    ++peer.Strikes;
    ++TotalStrikes;
    if (peer.Strikes < kNetMaxStrikes)
        return;

    // An admitted peer gets a disconnect, not a refusal: refusals are for the
    // door, and a client that was inside treats an unexplained end as a
    // network failure it might wait out.
    if (peer.State == NetPeerState::Connected)
    {
        Scratch scratch{};
        NetDisconnect message;
        message.Reason = std::string(why.substr(
            0, std::min(why.size(), NetDefaultCaps().MaxReasonBytes)));
        SendRaw(peer.Address, NetEncodeDisconnect(message, scratch));
    }
    else
    {
        RefuseAt(peer.Address, why);
    }
    peer.State = NetPeerState::Disconnected;
}

void NetSession::DeliverChannelPayloads(NetPeer& peer,
                                        std::span<const std::byte> packet,
                                        double nowSeconds,
                                        std::vector<Delivery>& out)
{
    NetDecodeError error = NetDecodeError::None;
    std::vector<std::vector<std::byte>> messages = peer.Channels.Receive(packet, error);
    if (error != NetDecodeError::None)
    {
        Strike(peer, NetDecodeErrorToString(error));
        return;
    }

    peer.LastHeardSeconds = nowSeconds;
    const auto channel = static_cast<NetChannelKind>(packet[0]);
    for (std::vector<std::byte>& message : messages)
    {
        out.push_back(Delivery{
            .From = peer.Id,
            .Channel = channel,
            .Payload = std::move(message),
        });
    }
}

void NetSession::HandleAuthorityMessage(const NetDatagram& datagram,
                                        double nowSeconds,
                                        std::vector<Delivery>& out)
{
    Scratch scratch{};
    NetMessageType type = NetMessageType::Invalid;

    // A packet from an admitted peer belongs to the channel layer; only the
    // control messages a live peer may still send -- disconnect and the
    // keepalives -- are peeked off first. Their type bytes sit above every
    // channel kind (asserted at the top of this file), so a channel packet can
    // never be mistaken for one of them.
    if (NetPeer* known = PeerByAddress(datagram.From);
        known != nullptr && known->State == NetPeerState::Connected)
    {
        if (NetPeekMessageType(datagram.Payload, type) == NetDecodeError::None)
        {
            if (type == NetMessageType::Disconnect)
            {
                known->State = NetPeerState::Disconnected;
                return;
            }
            // A peer that is already admitted here but still handshaking on its
            // own side lost the accept, and is asking again. Answering is what
            // closes that: the alternative is a client that retries until its
            // connect window runs out while the authority holds a peer it
            // believes is live, and every retry falls through to the channel
            // layer as garbage and earns a strike on the way.
            //
            // Both answers are idempotent. The challenge is a pure function of
            // the address, and the accept repeats the id this peer already has,
            // so neither admits anyone twice nor raises a second joined event.
            if (type == NetMessageType::Hello)
            {
                known->LastHeardSeconds = nowSeconds;
                const NetChallenge challenge{ .Cookie = CookieFor(datagram.From) };
                SendRaw(datagram.From, NetEncodeChallenge(challenge, scratch));
                return;
            }
            if (type == NetMessageType::CookieEcho)
            {
                NetCookieEcho echo;
                if (NetDecodeCookieEcho(datagram.Payload, echo) != NetDecodeError::None
                    || echo.Cookie != CookieFor(datagram.From))
                {
                    Strike(*known, "malformed handshake repeat");
                    return;
                }
                known->LastHeardSeconds = nowSeconds;
                const NetAccept accept{
                    .PeerId = known->Id.Value,
                    .ModuleFingerprint = LocalIdentity.ModuleFingerprint,
                    .ReplicationTableHash = LocalIdentity.ReplicationTableHash,
                    .WorldIdentity = LocalIdentity.WorldIdentity,
                    .FixedTickRateMilliHz = LocalIdentity.FixedTickRateMilliHz,
                    .AuthorityTick = LocalTickIndex,
                };
                SendRaw(datagram.From, NetEncodeAccept(accept, scratch));
                return;
            }
            if (type == NetMessageType::Ping)
            {
                NetPing ping;
                if (NetDecodePing(datagram.Payload, ping) != NetDecodeError::None)
                {
                    Strike(*known, "malformed ping");
                    return;
                }
                known->LastHeardSeconds = nowSeconds;
                const NetPong pong{
                    .SentMicroseconds = ping.SentMicroseconds,
                    .AuthorityTick = LocalTickIndex,
                };
                SendRaw(datagram.From, NetEncodePong(pong, scratch));
                return;
            }
            if (type == NetMessageType::Pong)
            {
                NetPong pong;
                if (NetDecodePong(datagram.Payload, pong) != NetDecodeError::None)
                {
                    Strike(*known, "malformed pong");
                    return;
                }
                known->LastHeardSeconds = nowSeconds;
                const std::uint64_t nowMicro = MicroFromSeconds(nowSeconds);
                if (nowMicro >= pong.SentMicroseconds)
                    known->RoundTripMicroseconds = nowMicro - pong.SentMicroseconds;
                return;
            }
        }
        DeliverChannelPayloads(*known, datagram.Payload, nowSeconds, out);
        return;
    }

    if (NetPeekMessageType(datagram.Payload, type) != NetDecodeError::None)
        return;  // Unroutable from an unknown address: ignored, not tracked.

    switch (type)
    {
    case NetMessageType::Hello:
    {
        NetHello hello;
        if (NetDecodeHello(datagram.Payload, hello) != NetDecodeError::None)
        {
            // No peer state exists yet, so there is nothing to strike. A bad
            // hello costs the authority one refusal and nothing else, which is
            // the point of doing this before allocating anything.
            RefuseAt(datagram.From, "protocol version mismatch");
            return;
        }
        NetChallenge challenge{ .Cookie = CookieFor(datagram.From) };
        SendRaw(datagram.From, NetEncodeChallenge(challenge, scratch));
        return;
    }
    case NetMessageType::CookieEcho:
    {
        NetCookieEcho echo;
        if (NetDecodeCookieEcho(datagram.Payload, echo) != NetDecodeError::None)
        {
            RefuseAt(datagram.From, "malformed handshake");
            return;
        }
        // Verified before any state exists for this address. A forged source
        // address never received the challenge, so it cannot echo this.
        if (echo.Cookie != CookieFor(datagram.From))
        {
            RefuseAt(datagram.From, "bad cookie");
            return;
        }

        const NetIdentity remote{
            .ModuleFingerprint = echo.ModuleFingerprint,
            .ReplicationTableHash = echo.ReplicationTableHash,
            .WorldIdentity = echo.WorldIdentity,
            .FixedTickRateMilliHz = echo.FixedTickRateMilliHz,
        };
        if (const std::string_view mismatch =
                NetDescribeIdentityMismatch(LocalIdentity, remote);
            !mismatch.empty())
        {
            RefuseAt(datagram.From, mismatch);
            return;
        }

        if (ConnectedPeers().size() >= MaxPeers)
        {
            RefuseAt(datagram.From, "session full");
            return;
        }

        NetPeer peer;
        peer.Id = NextPeerId;
        NextPeerId = PeerId{ NextPeerId.Value + 1 };
        peer.Address = datagram.From;
        peer.State = NetPeerState::Connected;
        peer.LastHeardSeconds = nowSeconds;
        Peers.push_back(std::move(peer));

        const NetAccept accept{
            .PeerId = Peers.back().Id.Value,
            .ModuleFingerprint = LocalIdentity.ModuleFingerprint,
            .ReplicationTableHash = LocalIdentity.ReplicationTableHash,
            .WorldIdentity = LocalIdentity.WorldIdentity,
            .FixedTickRateMilliHz = LocalIdentity.FixedTickRateMilliHz,
            .AuthorityTick = LocalTickIndex,
        };
        SendRaw(datagram.From, NetEncodeAccept(accept, scratch));
        Events.push_back(NetPeerEvent{
            .Kind = NetPeerEventKind::Joined,
            .Peer = Peers.back().Id,
            .Address = Peers.back().Address,
            .Reason = {},
        });
        return;
    }
    case NetMessageType::Ping:
    {
        NetPing ping;
        if (NetDecodePing(datagram.Payload, ping) != NetDecodeError::None)
            return;
        const NetPong pong{
            .SentMicroseconds = ping.SentMicroseconds,
            .AuthorityTick = LocalTickIndex,
        };
        SendRaw(datagram.From, NetEncodePong(pong, scratch));
        return;
    }
    default:
        // Anything else from an unadmitted address is out of protocol state.
        // Silently ignored rather than answered: replying would make this an
        // amplifier for a spoofed source.
        return;
    }
}

void NetSession::HandleClientMessage(const NetDatagram& datagram,
                                     double nowSeconds,
                                     std::vector<Delivery>& out)
{
    // Only the authority's address is listened to at all. This is the cheapest
    // possible filter against off-path injection, and it costs nothing.
    if (!(datagram.From == AuthorityAddress))
        return;
    AuthorityLastHeardSeconds = nowSeconds;

    Scratch scratch{};
    NetMessageType type = NetMessageType::Invalid;

    if (Admitted)
    {
        if (NetPeekMessageType(datagram.Payload, type) == NetDecodeError::None)
        {
            if (type == NetMessageType::Disconnect)
            {
                Failure = NetJoinFailure::Ended;
                FailureReason = "session ended";
                NetDisconnect message;
                if (NetDecodeDisconnect(datagram.Payload, message) == NetDecodeError::None)
                    FailureReason = message.Reason;
                Admitted = false;
                CurrentRole = NetSessionRole::Standalone;
                Transport.Close();
                return;
            }
            if (type == NetMessageType::Ping)
            {
                NetPing ping;
                if (NetDecodePing(datagram.Payload, ping) == NetDecodeError::None)
                {
                    const NetPong pong{
                        .SentMicroseconds = ping.SentMicroseconds,
                        .AuthorityTick = LastAuthorityTick,
                    };
                    SendRaw(AuthorityAddress, NetEncodePong(pong, scratch));
                }
                return;
            }
            if (type == NetMessageType::Pong)
            {
                NetPong pong;
                if (NetDecodePong(datagram.Payload, pong) == NetDecodeError::None)
                {
                    LastAuthorityTick = pong.AuthorityTick;
                    const std::uint64_t nowMicro = MicroFromSeconds(nowSeconds);
                    if (nowMicro >= pong.SentMicroseconds)
                        RttMicroseconds = nowMicro - pong.SentMicroseconds;
                }
                return;
            }
        }

        NetDecodeError error = NetDecodeError::None;
        std::vector<std::vector<std::byte>> messages =
            ClientChannels.Receive(datagram.Payload, error);
        if (error != NetDecodeError::None)
            return;

        const auto channel = static_cast<NetChannelKind>(datagram.Payload[0]);
        for (std::vector<std::byte>& message : messages)
        {
            out.push_back(Delivery{
                .From = SelfId,
                .Channel = channel,
                .Payload = std::move(message),
            });
        }
        return;
    }

    if (NetPeekMessageType(datagram.Payload, type) != NetDecodeError::None)
        return;

    switch (type)
    {
    case NetMessageType::Challenge:
    {
        NetChallenge challenge;
        if (NetDecodeChallenge(datagram.Payload, challenge) != NetDecodeError::None)
            return;
        if (!AwaitingChallenge)
            return;

        NetCookieEcho echo;
        echo.Cookie = std::move(challenge.Cookie);
        echo.ProtocolVersion = kNetProtocolVersion;
        echo.ModuleFingerprint = LocalIdentity.ModuleFingerprint;
        echo.ReplicationTableHash = LocalIdentity.ReplicationTableHash;
        echo.WorldIdentity = LocalIdentity.WorldIdentity;
        echo.FixedTickRateMilliHz = LocalIdentity.FixedTickRateMilliHz;
        AwaitingChallenge = false;
        SendRaw(AuthorityAddress, NetEncodeCookieEcho(echo, scratch));
        return;
    }
    case NetMessageType::Accept:
    {
        NetAccept accept;
        if (NetDecodeAccept(datagram.Payload, accept) != NetDecodeError::None)
            return;

        // The gate runs in this direction too. A client refuses a mismatched
        // authority before applying any state from it, which is the half of
        // the handshake a client-side threat model actually depends on.
        const NetIdentity remote{
            .ModuleFingerprint = accept.ModuleFingerprint,
            .ReplicationTableHash = accept.ReplicationTableHash,
            .WorldIdentity = accept.WorldIdentity,
            .FixedTickRateMilliHz = accept.FixedTickRateMilliHz,
        };
        if (const std::string_view mismatch =
                NetDescribeIdentityMismatch(LocalIdentity, remote);
            !mismatch.empty())
        {
            Failure = NetJoinFailure::Refused;
            FailureReason = std::string(mismatch);
            Disconnect(mismatch);
            return;
        }

        Admitted = true;
        SelfId = PeerId{ accept.PeerId };
        LastAuthorityTick = accept.AuthorityTick;
        return;
    }
    case NetMessageType::Refuse:
    {
        NetRefuse refuse;
        if (NetDecodeRefuse(datagram.Payload, refuse) != NetDecodeError::None)
            return;
        Failure = NetJoinFailure::Refused;
        FailureReason = refuse.Reason;
        CurrentRole = NetSessionRole::Standalone;
        Transport.Close();
        return;
    }
    default:
        return;
    }
}

std::vector<NetSession::Delivery> NetSession::Pump(double nowSeconds)
{
    std::vector<Delivery> out;
    Events.clear();
    if (CurrentRole == NetSessionRole::Standalone)
        return out;

    for (const NetDatagram& datagram : Transport.Receive())
    {
        if (datagram.Payload.empty())
            continue;
        if (CurrentRole == NetSessionRole::Host)
            HandleAuthorityMessage(datagram, nowSeconds, out);
        else
            HandleClientMessage(datagram, nowSeconds, out);
    }

    if (CurrentRole == NetSessionRole::Host)
    {
        // A peer that has gone quiet is dropped rather than held forever: its
        // slot, its channel buffers, and its place in the peer cap are the
        // resources a half-open connection would otherwise hold. It is told,
        // best effort: the return path often still works when nothing has
        // arrived, and one datagram spares the peer its own full timeout.
        for (NetPeer& peer : Peers)
        {
            if (peer.State != NetPeerState::Connected)
                continue;
            if ((nowSeconds - peer.LastHeardSeconds) > TimeoutSeconds)
            {
                Scratch scratch{};
                SendRaw(peer.Address,
                        NetEncodeDisconnect(NetDisconnect{ .Reason = "timed out" },
                                            scratch));
                peer.State = NetPeerState::Disconnected;
            }
        }
        for (const NetPeer& peer : Peers)
        {
            if (peer.State != NetPeerState::Disconnected)
                continue;
            Events.push_back(NetPeerEvent{
                .Kind = NetPeerEventKind::Left,
                .Peer = peer.Id,
                .Address = peer.Address,
                .Reason = (nowSeconds - peer.LastHeardSeconds) > TimeoutSeconds
                              ? "timed out"
                              : "disconnected",
            });
        }
        std::erase_if(Peers, [](const NetPeer& peer) {
            return peer.State == NetPeerState::Disconnected;
        });
    }
    else if (CurrentRole == NetSessionRole::Client)
    {
        if (!Admitted)
        {
            if (!ConnectClockArmed)
            {
                ConnectClockArmed = true;
                ConnectStartedSeconds = nowSeconds;
                // Connect sent one already; the retry cadence runs from it.
                LastHandshakeSentSeconds = nowSeconds;
            }
            else if ((nowSeconds - ConnectStartedSeconds) > TimeoutSeconds)
            {
                Failure = NetJoinFailure::TimedOut;
                FailureReason = "no response from authority";
                CurrentRole = NetSessionRole::Standalone;
                Transport.Close();
            }
            else if ((nowSeconds - LastHandshakeSentSeconds) >= HandshakeRetrySeconds())
            {
                // Starting the handshake over rather than resuming it, because
                // the authority holds no state to resume from until the cookie
                // comes back -- that is what makes a challenge cheap to ask for
                // again.
                LastHandshakeSentSeconds = nowSeconds;
                SendHello();
            }
        }
        else if ((nowSeconds - AuthorityLastHeardSeconds) > TimeoutSeconds)
        {
            // Admission set the last-heard clock, so this measures silence
            // since then -- the host's keepalives are what normally keep it
            // fresh, and their absence is what "the host is gone" looks like.
            Failure = NetJoinFailure::TimedOut;
            FailureReason = "authority stopped responding";
            Admitted = false;
            CurrentRole = NetSessionRole::Standalone;
            Transport.Close();
        }
    }

    return out;
}

void NetSession::Flush(double nowSeconds)
{
    // Keepalives ride the flush: an admitted end with nothing to say must
    // still be distinguishable from a dead one, and the echoed timestamp is
    // the round-trip measurement for free. Channel traffic does not reset the
    // cadence; a fixed cadence is simpler to reason about and four pings per
    // timeout are noise next to any real traffic.
    Scratch scratch{};

    if (CurrentRole == NetSessionRole::Host)
    {
        for (NetPeer& peer : Peers)
        {
            if (peer.State != NetPeerState::Connected)
                continue;
            if ((nowSeconds - peer.LastPingSentSeconds) >= KeepaliveSeconds())
            {
                peer.LastPingSentSeconds = nowSeconds;
                const NetPing ping{ .SentMicroseconds = MicroFromSeconds(nowSeconds) };
                SendRaw(peer.Address, NetEncodePing(ping, scratch));
            }
            for (const NetChannelSet::Packet& packet : peer.Channels.Drain(nowSeconds))
                SendRaw(peer.Address, packet.Bytes);
        }
        return;
    }

    if (CurrentRole == NetSessionRole::Client && Admitted)
    {
        if ((nowSeconds - LastPingSentSeconds) >= KeepaliveSeconds())
        {
            LastPingSentSeconds = nowSeconds;
            const NetPing ping{ .SentMicroseconds = MicroFromSeconds(nowSeconds) };
            SendRaw(AuthorityAddress, NetEncodePing(ping, scratch));
        }
        for (const NetChannelSet::Packet& packet : ClientChannels.Drain(nowSeconds))
            SendRaw(AuthorityAddress, packet.Bytes);
    }
}

bool NetSession::Send(PeerId peer, NetChannelKind channel,
                      std::span<const std::byte> message)
{
    if (CurrentRole == NetSessionRole::Client)
    {
        if (!Admitted)
            return false;
        return ClientChannels.Send(channel, message);
    }

    for (NetPeer& candidate : Peers)
    {
        if (candidate.Id == peer && candidate.State == NetPeerState::Connected)
            return candidate.Channels.Send(channel, message);
    }
    return false;
}

void NetSession::Broadcast(NetChannelKind channel, std::span<const std::byte> message)
{
    if (CurrentRole == NetSessionRole::Client)
    {
        (void)Send(SelfId, channel, message);
        return;
    }

    for (NetPeer& peer : Peers)
    {
        if (peer.State == NetPeerState::Connected)
            (void)peer.Channels.Send(channel, message);
    }
    (void)DescribeChannel(channel);
}
