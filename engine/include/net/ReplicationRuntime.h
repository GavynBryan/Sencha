#pragma once

#include <net/NetSession.h>
#include <net/NetSpawnRecipe.h>
#include <net/ReplicationSnapshot.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

class World;
class WorldComponentSchema;

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
    };

    // Authority side. Writes one snapshot per connected peer and queues it on
    // the unreliable channel: a snapshot that arrives late is worthless, since
    // the next one supersedes it, so there is nothing to gain from resending.
    PublishStats Publish(NetSession& session, World& world,
                         const ReplicationLayout& layout, std::uint64_t tick);

    // Client side. `payload` is one channel message, still carrying its kind
    // byte. Returns what happened; a payload that is not a snapshot is ignored
    // rather than treated as an error, because other kinds share this channel.
    SnapshotApplyResult Apply(std::span<const std::byte> payload, World& world,
                              const WorldComponentSchema& schema,
                              const ReplicationLayout& layout,
                              const NetSpawnRecipes* recipes = nullptr,
                              ClientPrediction* prediction = nullptr,
                              ReplicationInterpolation* interpolation = nullptr);

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
    std::unordered_map<PeerId, ReplicationPeerState> Peers;
    ReplicationClientIdentity ClientMap;
    // Reused across peers and frames. Sized once to the largest datagram a
    // channel will fragment for us, so publishing allocates nothing per frame.
    std::vector<std::byte> Scratch;
};
