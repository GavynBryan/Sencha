#pragma once

#include <core/identity/StrongId.h>
#include <net/NetChannel.h>
#include <net/NetProtocol.h>
#include <net/NetTransport.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

//=============================================================================
// NetSession
//
// Peer lifecycle: who is connected, how they got there, and what they are
// allowed to have said on the way. It owns the channels per peer and the
// handshake state machine, and it owns no sockets -- the transport is injected,
// so a whole session runs in process over a loopback pair with no ports and no
// scheduling in the way.
//
// One session model, three configurations, one binary:
//
//   - Host: authority plus a local player. The listen server, and with no
//     remote peers, exactly single-player.
//   - Client: mirrors replicated state, sends inputs and requests.
//   - Headless host: authority with no local player. The dedicated server.
//
// No session constructed means no networking in the frame at all, which is what
// keeps single-player free of all of this.
//=============================================================================

using PeerId = StrongId<struct PeerIdTag, std::uint32_t>;

enum class NetSessionRole : std::uint8_t
{
    // Not in a session. Every net frame phase is a no-op.
    Standalone,
    Host,
    Client,
};

enum class NetPeerState : std::uint8_t
{
    // The authority has seen a Hello and replied with a cookie, but holds no
    // state for this address yet -- the cookie is what makes that possible.
    Challenged,
    Connected,
    Disconnected,
};

// Why a client's session or connection attempt ended, for the console and the
// log. Kept distinct from the free-text reason so callers branch on the cause
// rather than on message text.
enum class NetJoinFailure : std::uint8_t
{
    None,
    Refused,        // the authority said no at the door, with a reason
    TimedOut,       // never answered, or went silent after admission
    TransportError,
    Ended,          // admitted, then ended by the authority: kicked or it quit
};

//=============================================================================
// Identity gate
//
// What both ends compare before any state exists. Mutual by design: the client
// refuses a mismatched authority the same way the authority refuses a
// mismatched client, because a compromised server is in the threat model.
//
// It is an assertion, not a proof. A machine an attacker controls can report
// whatever it likes here, so nothing about client safety may rest on it -- it
// stops accidents and version confusion, and the decode boundary handles the
// rest.
//=============================================================================
struct NetIdentity
{
    std::uint64_t ModuleFingerprint = 0;
    std::uint64_t ReplicationTableHash = 0;
    std::uint64_t WorldIdentity = 0;
    std::uint32_t FixedTickRateMilliHz = 60000;

    bool operator==(const NetIdentity&) const = default;
};

// Why an identity was refused, so the message names the field rather than
// saying only that something did not match.
[[nodiscard]] std::string_view NetDescribeIdentityMismatch(const NetIdentity& local,
                                                           const NetIdentity& remote);

struct NetPeer
{
    PeerId Id;
    NetAddress Address;
    NetPeerState State = NetPeerState::Challenged;
    NetChannelSet Channels;
    // Strikes are how a peer earns a disconnect. A decode error, a cap
    // violation, or a protocol-state violation each count one; the peer is
    // dropped at the limit rather than on the first anomaly, because ordinary
    // crosstalk on a shared port is not an attack.
    std::uint32_t Strikes = 0;
    double LastHeardSeconds = 0.0;
    double LastPingSentSeconds = 0.0;
    std::uint64_t RoundTripMicroseconds = 0;
    // Why the peer left, recorded at the transition to Disconnected -- the one
    // place the cause is known -- and reported in that pump's leave event.
    std::string LeaveReason;
};

// A peer is dropped at this many strikes.
inline constexpr std::uint32_t kNetMaxStrikes = 8;

// Peers the design is validated against. Four is the co-op shape and stays the
// default a host admits; sixteen is what the arena modes need and what the
// replication budget, fill order, and per-peer state are measured at.
//
// The number moved because what made eight the ceiling was removed rather than
// raised: a snapshot used to carry every entity to every peer at full width and
// per-tick, and the authority's whole outbound bill scaled with the product.
// Change tracking, a per-peer byte budget that defers instead of failing, and a
// quantized transform cut that product enough that the peer count stopped being
// what binds. Held at sixteen because that is what is measured, not because
// something breaks at seventeen.
inline constexpr std::size_t kNetMaxPeersSupported = 16;

// What changed about the peer set during a pump. The game needs these to spawn
// and despawn a pawn per player, and a headless host needs them because console
// output goes to a console view nobody is watching on a server.
enum class NetPeerEventKind : std::uint8_t
{
    Joined,
    Left,
};

struct NetPeerEvent
{
    NetPeerEventKind Kind = NetPeerEventKind::Joined;
    PeerId Peer;
    NetAddress Address;
    // Empty for a join; why they went, for a leave.
    std::string Reason;
};

class NetSession
{
public:
    // Messages the session hands up after decoding and admission. The payload
    // is owned, because the transport's buffers are gone by the time anything
    // above the session runs.
    struct Delivery
    {
        PeerId From;
        NetChannelKind Channel;
        std::vector<std::byte> Payload;
    };

    explicit NetSession(INetTransport& transport);

    // Binds and becomes the authority. Port zero takes an ephemeral one.
    [[nodiscard]] bool Host(std::uint16_t port, const NetIdentity& identity);
    // Binds an ephemeral port and begins the handshake with `authority`.
    [[nodiscard]] bool Connect(const NetAddress& authority, const NetIdentity& identity);
    void Disconnect(std::string_view reason);

    [[nodiscard]] NetSessionRole Role() const { return CurrentRole; }
    [[nodiscard]] bool IsConnected() const;
    [[nodiscard]] const NetAddress& LocalAddress() const { return Local; }

    // Drains the transport, advances the handshake, and returns whatever
    // arrived for the layers above. Called from the frame's net pump phase.
    [[nodiscard]] std::vector<Delivery> Pump(double nowSeconds);
    // Puts queued channel traffic on the wire. Called after simulation, so a
    // snapshot leaves in the frame that produced it.
    void Flush(double nowSeconds);

    // Queues to one peer, or to every connected peer.
    [[nodiscard]] bool Send(PeerId peer, NetChannelKind channel,
                            std::span<const std::byte> message);
    void Broadcast(NetChannelKind channel, std::span<const std::byte> message);

    // Peer transitions from the most recent Pump. Cleared at the start of each
    // one, so a caller that pumps and then reads sees exactly that frame's.
    [[nodiscard]] std::span<const NetPeerEvent> PeerEvents() const { return Events; }

    [[nodiscard]] std::vector<PeerId> ConnectedPeers() const;
    [[nodiscard]] const NetPeer* FindPeer(PeerId id) const;
    [[nodiscard]] std::size_t PeerCount() const { return Peers.size(); }

    void SetMaxPeers(std::size_t count) { MaxPeers = count; }
    void SetTimeoutSeconds(double seconds) { TimeoutSeconds = seconds; }

    // The world this session is running. An authority announces it in every
    // admission so a joining client can load it instead of having to be told by
    // whoever launched it; a client reads back whatever it was announced.
    //
    // One member for both roles because it is one fact -- which world this
    // session is about -- and the role decides who writes it. Truncated here
    // rather than at the encoder, so the wire cap is enforced where a caller can
    // still see what it lost.
    void SetAnnouncedMap(std::string_view map);
    [[nodiscard]] const std::string& AnnouncedMap() const { return AnnouncedMapName; }

    // The fixed tick this machine is simulating. An authority publishes it in
    // every admission and keepalive, because two machines counting their own
    // ticks from their own process start share no name for a moment in time
    // until one of them says which name it uses.
    void SetLocalTick(std::uint64_t tick) { LocalTickIndex = tick; }
    [[nodiscard]] std::uint64_t LocalTick() const { return LocalTickIndex; }

    // Client-side outcome of the last connection attempt.
    [[nodiscard]] NetJoinFailure JoinFailure() const { return Failure; }
    [[nodiscard]] const std::string& JoinFailureReason() const { return FailureReason; }
    [[nodiscard]] PeerId LocalPeerId() const { return SelfId; }
    [[nodiscard]] std::uint64_t AuthorityTick() const { return LastAuthorityTick; }
    // Who the client dialed, and the round trip its keepalives measure. The
    // host reads per-peer round trips off NetPeer instead.
    [[nodiscard]] const NetAddress& Authority() const { return AuthorityAddress; }
    [[nodiscard]] std::uint64_t RoundTripMicroseconds() const { return RttMicroseconds; }

    // Diagnostics for the status command.
    [[nodiscard]] std::uint64_t StrikesIssued() const { return TotalStrikes; }
    [[nodiscard]] std::uint64_t Refusals() const { return TotalRefusals; }

private:
    [[nodiscard]] NetPeer* PeerByAddress(const NetAddress& address);
    void HandleAuthorityMessage(const NetDatagram& datagram, double nowSeconds,
                                std::vector<Delivery>& out);
    void HandleClientMessage(const NetDatagram& datagram, double nowSeconds,
                             std::vector<Delivery>& out);
    // Keepalive cadence, derived from the timeout so several pings fit inside
    // one timeout window whatever the timeout is set to.
    [[nodiscard]] double KeepaliveSeconds() const { return TimeoutSeconds * 0.25; }
    // How often an unadmitted client repeats its handshake step. Faster than a
    // keepalive because the connect window is the whole budget: the handshake is
    // the one exchange with no next message to supersede a lost one, so a
    // datagram lost in any of its four legs has to be replaced rather than
    // waited out.
    [[nodiscard]] double HandshakeRetrySeconds() const { return TimeoutSeconds * 0.1; }
    void SendHello();
    // The admission message, assembled from the session's own identity and
    // announcements. One builder for the fresh admission and the repeat sent to
    // a peer whose accept was lost, so the two cannot drift apart.
    void SendAccept(const NetAddress& to, PeerId peer);
    void DeliverChannelPayloads(NetPeer& peer, std::span<const std::byte> packet,
                                double nowSeconds, std::vector<Delivery>& out);
    void Strike(NetPeer& peer, std::string_view why);
    void SendRaw(const NetAddress& to, std::span<const std::byte> bytes);
    void RefuseAt(const NetAddress& address, std::string_view reason);
    // The cookie is an HMAC-shaped fold over the address and a per-session
    // secret. It exists so the authority can verify a peer reached the address
    // it claims before allocating anything for it, which is what makes a
    // spoofed source address unable to start a session.
    [[nodiscard]] std::vector<std::byte> CookieFor(const NetAddress& address) const;

    INetTransport& Transport;
    NetSessionRole CurrentRole = NetSessionRole::Standalone;
    NetIdentity LocalIdentity;
    NetAddress Local;
    NetAddress AuthorityAddress;

    std::vector<NetPeer> Peers;
    PeerId NextPeerId{ 1 };
    std::size_t MaxPeers = 4;
    double TimeoutSeconds = 10.0;
    std::uint64_t Secret = 0;
    std::uint64_t LocalTickIndex = 0;

    // Client state.
    NetChannelSet ClientChannels;
    bool AwaitingChallenge = false;
    bool Admitted = false;
    PeerId SelfId;
    std::uint64_t LastAuthorityTick = 0;
    // Armed by the first pump after Connect, because Connect has no clock. A
    // flag rather than a zero sentinel: time zero is a legitimate first pump.
    bool ConnectClockArmed = false;
    double ConnectStartedSeconds = 0.0;
    double LastHandshakeSentSeconds = 0.0;
    double AuthorityLastHeardSeconds = 0.0;
    double LastPingSentSeconds = 0.0;
    std::uint64_t RttMicroseconds = 0;
    NetJoinFailure Failure = NetJoinFailure::None;
    std::string FailureReason;
    // Set by the host through the setter, and by a client from the admission it
    // was accepted with.
    std::string AnnouncedMapName;

    std::vector<NetPeerEvent> Events;
    std::uint64_t TotalStrikes = 0;
    std::uint64_t TotalRefusals = 0;
};
