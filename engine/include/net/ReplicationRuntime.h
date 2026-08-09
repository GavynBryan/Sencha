#pragma once

#include <net/NetSession.h>
#include <net/NetSpawnRecipe.h>
#include <net/PeerCommandRuntime.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationSnapshot.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

class World;
class WorldComponentSchema;

// Ticks between snapshots out of the box: 30Hz against a 60Hz simulation. The
// rate a snapshot is worth sending at is set by how fast a player can perceive
// a change, not by how fast the world is stepped, and the interpolation buffer
// covers the difference. Halving the rate halves the authority's outbound bill
// per peer, which is the term that decides how many players fit.
inline constexpr std::uint32_t kNetDefaultSnapshotInterval = 2;

//=============================================================================
// ReplicationRuntime
//
// The session-facing half of replication: it owns what has to persist between
// frames -- the authority's identity mint, one baseline per connected peer, and
// a client's map of what it has been told about -- and turns that into
// snapshots leaving and snapshots arriving.
//
// Everything below it is pure. This is where per-peer state lives, so it is
// also where a peer joining or leaving is handled: a new peer starts with no
// baseline and therefore gets full state, and a departed one's baseline is
// released rather than kept against a peer that will never ack again.
//=============================================================================
class ReplicationRuntime
{
public:
    struct PublishStats
    {
        std::uint32_t PeersServed = 0;
        std::uint32_t SnapshotsSent = 0;
        std::size_t BytesQueued = 0;
        // Summed over the peers served. Deferred entities are the budget doing
        // its job and are only worth watching as a trend; unsendable ones are
        // never worth anything but a fix, since nothing about them can reach the
        // peer at all.
        std::uint32_t EntitiesDeferred = 0;
        std::uint32_t EntitiesUnsendable = 0;
    };

    // Authority side. Writes one snapshot per connected peer and queues it on
    // the unreliable channel: a snapshot that arrives late is worthless, since
    // the next one supersedes it, so there is nothing to gain from resending.
    // `commands` supplies each peer's acknowledgement -- how far the authority
    // has got through that peer's input -- so a snapshot tells a client both
    // what the world is and which of the client's own guesses it accounts for.
    // Null acknowledges nothing, which is right for a recording.
    //
    // Called every frame; whether it publishes is its own business (see
    // SetPublishInterval). Peer bookkeeping happens on every call regardless of
    // cadence, so a peer that leaves stops costing memory at once.
    PublishStats Publish(NetSession& session, World& world,
                         const ReplicationLayout& layout, std::uint64_t tick,
                         const PeerCommandRuntime* commands = nullptr);

    // Simulation ticks between snapshots. One publishes as fast as the world
    // moves; higher trades freshness for bandwidth, which is the trade that
    // decides how many players fit in a session -- the cost of a snapshot is
    // paid per peer, so halving the rate halves the authority's whole outbound
    // bill. Mirrored motion stays smooth because the interpolation buffer
    // already presents between samples rather than stepping on arrival.
    //
    // Zero and one both mean every tick.
    void SetPublishInterval(std::uint32_t ticks) { PublishInterval = ticks; }
    [[nodiscard]] std::uint32_t PublishIntervalTicks() const
    {
        return PublishInterval;
    }

    // Client side. `payload` is one channel message, still carrying its kind
    // byte. Returns what happened; a payload that is not a snapshot is ignored
    // rather than treated as an error, because other kinds share this channel.
    SnapshotApplyResult Apply(std::span<const std::byte> payload, World& world,
                              const WorldComponentSchema& schema,
                              const ReplicationLayout& layout,
                              const NetSpawnRecipes* recipes = nullptr,
                              ClientPrediction* prediction = nullptr,
                              ReplicationInterpolation* interpolation = nullptr);

    // Client side. The newest snapshot tick this machine has applied. Used for
    // the shared clock; what the authority is told about delivery is the ack
    // below, which names snapshots rather than moments.
    [[nodiscard]] std::uint64_t AppliedSnapshot() const { return AppliedTick; }

    // Client side. Which snapshots this machine has actually applied, which is
    // what travels back so the next difference is measured from a state it
    // really reached.
    [[nodiscard]] const NetSnapshotAck& AppliedAck() const { return AppliedAcks; }

    // A peer that left keeps no baseline: it would be a growing memory cost
    // against a peer that will never receive anything again, and a peer id can
    // be reused.
    void ForgetPeer(PeerId peer);
    // Session over. Identity and baselines are session-transient by definition.
    void Reset();

    [[nodiscard]] std::size_t TrackedPeers() const { return Peers.size(); }
    [[nodiscard]] const ReplicationClientIdentity& ClientEntities() const
    {
        return ClientMap;
    }

private:
    ReplicationAuthorityIdentity Identity;
    ReplicationChangeStore Changes;
    // Counts publishes, and is what "since" means in every floor and every
    // change stamp. Its own counter rather than the simulation tick, because it
    // must increase on every pass and a tick need not.
    std::uint64_t Generation = 0;
    std::unordered_map<PeerId, ReplicationPeerState> Peers;
    ReplicationClientIdentity ClientMap;
    std::uint64_t AppliedTick = 0;
    NetSnapshotAck AppliedAcks;

    // The shipping cadence, not one-per-tick: a cvar's OnChange fires on
    // change, so a default that disagreed with the cvar's would mean the
    // engine ran at a rate nobody chose until someone happened to set it.
    std::uint32_t PublishInterval = kNetDefaultSnapshotInterval;
    // The tick a snapshot last went out on, and whether one ever has. Compared
    // as a difference rather than a remainder on purpose: a host running slower
    // than its tick rate advances the tick index by several per frame, and a
    // remainder test on a stride it never lands on would stop publishing for
    // the rest of the session.
    std::uint64_t LastPublishedTick = 0;
    bool HasPublished = false;
    // Reused across peers and frames. Sized once to the largest datagram a
    // channel will fragment for us, so publishing allocates nothing per frame.
    std::vector<std::byte> Scratch;
};
