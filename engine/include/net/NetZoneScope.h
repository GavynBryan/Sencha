#pragma once

#include <zone/ZoneId.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// Zone scope: which zones a peer holds, and which it has proved it holds
//
// A session that streams a world cannot send a peer state for a room it has
// not loaded. There is nowhere to put it: the entities are in a storage
// partition that does not exist on that machine yet, and an applier that
// creates them anyway builds a second copy of a room the client is about to
// load properly.
//
// So residency is negotiated rather than assumed, in three messages:
//
//   Grant   authority -> peer   "hold this zone"
//   Ack     peer -> authority   "I have it, attached and finalized"
//   Revoke  authority -> peer   "you can let it go"
//
// The invariant that makes it worth the round trip: the authority never sends
// entity state for a zone the peer has not acked. A grant is not permission to
// receive, it is an instruction to load; the ack is what opens the zone.
//
// All three ride the reliable ordered channel. None of them has a next one that
// supersedes it -- a lost grant is a room the client never loads, a lost revoke
// is one it holds forever, a lost ack is a room the authority never fills --
// and the order matters as much as the delivery, because grant, revoke, grant
// arriving in any other order describes a different world.
//=============================================================================

enum class NetZoneScopeVerb : std::uint8_t
{
    Grant = 1,
    Revoke = 2,
};

struct NetZoneScopeUpdate
{
    ZoneId Zone;
    NetZoneScopeVerb Verb = NetZoneScopeVerb::Grant;
};

// One kind carrying a verb rather than two kinds. They are the same fact about
// the same zone read in two directions, and one decoder is one place for a
// client to be wrong about zone scope instead of two.
//
// Encodes into `out`, writing the payload-kind byte itself. Returns the bytes
// used, or zero if it would not fit.
[[nodiscard]] std::size_t NetEncodeZoneScopeUpdate(const NetZoneScopeUpdate& update,
                                                   std::span<std::byte> out);
[[nodiscard]] bool NetDecodeZoneScopeUpdate(std::span<const std::byte> bytes,
                                            NetZoneScopeUpdate& out);

// The peer's half. Carries only the zone: which peer said it is the session's
// to know, and a message that named its own sender would be a message a peer
// could lie in.
[[nodiscard]] std::size_t NetEncodeZoneAck(ZoneId zone, std::span<std::byte> out);
[[nodiscard]] bool NetDecodeZoneAck(std::span<const std::byte> bytes, ZoneId& out);

enum class NetZoneScopeState : std::uint8_t
{
    // Not this peer's to hold. Also what a revoked zone becomes: there is no
    // difference between a room a peer never had and one it has let go.
    None = 0,
    // Told to load it. Nothing may be sent about it yet.
    Granted,
    // It said it has it. State for the zone flows from here.
    Acked,
};

// One peer's zone scope. Held per peer beside everything else that peer has
// been told, because that is exactly what this is.
class NetZoneScope
{
public:
    struct Entry
    {
        ZoneId Zone;
        NetZoneScopeState State = NetZoneScopeState::None;
    };

    // Authority side. Each returns whether the call changed anything, which is
    // also whether there is a message to send: re-granting a zone the peer is
    // already loading is not news, and sending it again would be an instruction
    // to load a room it is in the middle of loading.
    bool Grant(ZoneId zone);
    bool Revoke(ZoneId zone);

    // The peer confirmed it holds the zone. False for a zone it was never
    // granted -- which is a peer claiming a room nobody offered it, and a
    // protocol violation rather than something to absorb quietly.
    //
    // Acking twice is not that. The reliable channel does not deliver a message
    // twice, but a client that acks on every attach would, and answering an
    // honest peer with a strike is worse than answering it with nothing.
    bool Acknowledge(ZoneId zone);

    [[nodiscard]] NetZoneScopeState StateOf(ZoneId zone) const;

    // The flow-control question, and the only one the snapshot writer asks.
    //
    // An invalid zone is the persistent partition, which every peer has by
    // definition and which no grant gates: a session's own spawned entities
    // live there, and gating them would mean a player could not be told about
    // the pawn they are driving.
    [[nodiscard]] bool CanReceive(ZoneId zone) const;

    // Ascending by zone id, so a diagnostic reads the same twice.
    [[nodiscard]] std::span<const Entry> Entries() const { return Zones_; }
    [[nodiscard]] std::size_t Size() const { return Zones_.size(); }

    void Clear() { Zones_.clear(); }

private:
    [[nodiscard]] Entry* Find(ZoneId zone);
    [[nodiscard]] const Entry* Find(ZoneId zone) const;

    // Sorted by zone id. A handful of entries per peer -- the resident set is
    // bounded by the streaming cap -- so this is a scan over a cache line or
    // two rather than a hash probe, and it makes the order a contract.
    std::vector<Entry> Zones_;
};
