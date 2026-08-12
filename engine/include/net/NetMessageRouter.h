#pragma once

#include <net/NetProtocol.h>
#include <net/NetSession.h>
#include <net/NetStats.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

class ReplicationRuntime;
class World;

//=============================================================================
// NetMessageRouter
//
// Where a game's own payload kinds are answered.
//
// The engine already dispatches on the first byte of every channel payload --
// snapshot, command, cvar -- and handed everything else up as a span of
// deliveries that nothing in the tree ever read. This is that same dispatch,
// one table wider, so a game's message is answered the way a command is rather
// than by a hand-written scan over bytes in some system.
//
// A table of kinds, not of types. No reflection, no registration by C++ type,
// no return value, no correlation between a message and a reply. A handler is
// given bytes and the peer they came from, and what it does with them is the
// game's business.
//
// What this owns is the four things a hand-written parser gets wrong: which
// byte it may use, which direction a kind is legal in, that the peer a handler
// is told about is the session's rather than the message's, and that the bytes
// are counted.
//=============================================================================

// Which way a kind may travel. Checked before a handler is reached: a client
// that sends an authority-to-client kind is a different build or something
// probing, and neither is a handler's problem to recognise.
enum class NetMessageDirection : std::uint8_t
{
    ClientToAuthority,
    AuthorityToClient,
};

struct NetMessageContext
{
    // Taken from the delivery, which the session filled from the peer record it
    // verified. Which peer sent something is the one fact a message is never
    // allowed to claim about itself.
    PeerId From;
    World& Entities;
    // Where a wire identity in the body becomes a local entity: the authority's
    // map on a host, the client's on a client. An object is named through this
    // and through nothing else, because an identity is a field the sender
    // chooses. Null in a test with no replication running.
    const ReplicationRuntime* Objects = nullptr;
    // The payload with its kind byte stripped, so a decoder starts at its own
    // first field.
    std::span<const std::byte> Body;
};

// False means the message did not decode, or did not pass the game's own
// validation. Both are the same answer to the session: this peer is sending
// things this build will not accept.
using NetMessageHandler = bool (*)(void* context, const NetMessageContext&);

class NetMessageRouter
{
public:
    // Binds one kind. False for a kind outside the game range, a null handler,
    // or a kind something already answers -- two answers to one question is not
    // a configuration, and the second one silently winning is how a message
    // ends up handled by the wrong feature.
    [[nodiscard]] bool Bind(std::uint8_t kind, NetMessageDirection direction,
                            NetMessageHandler handler, void* context);

    // Routes one delivered payload's body. False for a kind nothing bound, a
    // kind arriving from the wrong direction, or a handler that refused; the
    // caller turns that into a strike.
    [[nodiscard]] bool Route(NetSessionRole role, std::uint8_t kind,
                             const NetMessageContext& message) const;

    [[nodiscard]] bool IsBound(std::uint8_t kind) const;
    [[nodiscard]] std::size_t BoundKinds() const;

    // Registered by the game and outlives any one session, because it describes
    // what the game says rather than who it is connected to. Cleared with the
    // game module, not with a disconnect.
    void Clear();

private:
    struct Entry
    {
        NetMessageHandler Handler = nullptr;
        void* Context = nullptr;
        NetMessageDirection Direction = NetMessageDirection::ClientToAuthority;
    };

    // Indexed by kind minus the first game kind. Dense because the range is
    // small and fixed, and a hash probe per arriving datagram is a lookup an
    // array does for free.
    std::array<Entry, kNetGamePayloadKindCount> Entries{};
};

//-----------------------------------------------------------------------------
// Sending
//
// Free functions rather than a sender type: there is no state to own. Three
// rather than one, because who a message goes to differs by role in a way a
// PeerId argument hides -- a client reaches the authority by sending to its own
// id, which reads as a mistake at every call site that does it.
//
// Each stamps the kind byte, which is the thing a hand-written sender
// eventually forgets, and each records what it queued, which is why outbound
// game traffic appeared nowhere in the stats surface.
//
// The channel is the caller's. A request that must not be lost rides reliable;
// something the next one of its kind supersedes rides unreliable. Nothing here
// can tell which, because that is a fact about the message.
//
// All three return the bytes queued, or zero if nothing was sent.
//-----------------------------------------------------------------------------
std::size_t NetSendToAuthority(NetSession& session, NetChannelKind channel,
                               std::uint8_t kind, std::span<const std::byte> body,
                               NetStats* traffic);

std::size_t NetSendToPeer(NetSession& session, PeerId to, NetChannelKind channel,
                          std::uint8_t kind, std::span<const std::byte> body,
                          NetStats* traffic);

std::size_t NetBroadcastToPeers(NetSession& session, NetChannelKind channel,
                                std::uint8_t kind, std::span<const std::byte> body,
                                NetStats* traffic);
