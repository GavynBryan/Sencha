#pragma once

#include <net/ReplicationRuntime.h>
#include <zone/ZoneDemand.h>

#include <cstdint>
#include <span>
#include <vector>

class AsyncZoneLoader;
class NetStats;
class RuntimeWorld;
class World;
class WorldPartitionRuntime;

//=============================================================================
// Keeping streaming and the session in agreement
//
// A game hands the engine its partition runtime (Engine::SetWorldStreaming) and
// nothing else. Everything below happens on its own, in the right order, every
// frame:
//
//   - the world stays loaded around the entity this machine drives;
//   - hosting, it stays loaded around every connected player as well, and each
//     of them is offered only its own neighbourhood;
//   - as a client, whatever the authority granted is loaded whether or not this
//     machine's own policy wanted it.
//
// This is one type because it was four calls a game had to make in one exact
// order, and getting the order wrong was silent: focus before the streaming
// update or a player streams a frame behind themselves, grants after it or a
// peer is offered rooms from before it moved. A game had no way to know that,
// and no reason to.
//=============================================================================
class NetZoneStreaming
{
public:
    // Once per frame, immediately before the partition's own update. `session`
    // is null outside a session, where this is just streaming around the local
    // player and every net path below is skipped.
    void Update(const World& world, EntityId localControlSubject,
                NetSession* session,
                ReplicationRuntime& replication, WorldPartitionRuntime& partition,
                NetStats* traffic);

    // The focus source an entity streams under. Keyed by entity rather than by
    // peer, because a peer can drive more than one thing at once -- somebody in
    // a turret still has a body sitting in it, and the room under that body has
    // to stay simulated for them to get back into it.
    //
    // The high bit keeps these clear of the source a locally driven pawn uses.
    // Slot indices are bounded by the world's entity table and do not reach it.
    [[nodiscard]] static FocusSourceId SourceFor(EntityId entity);

    // What each peer was offered on the last update, ascending by peer. For
    // diagnostics and tests; the spans point into storage the next update
    // rewrites.
    [[nodiscard]] std::span<const NetPeerZoneInterest> Interest() const
    {
        return Interest_;
    }

    // Sources currently held for peers, and zones currently pinned because the
    // authority granted them. Diagnostics.
    [[nodiscard]] std::span<const FocusSourceId> PeerSources() const
    {
        return Held_;
    }
    [[nodiscard]] std::span<const ZoneId> PinnedGrants() const { return Pinned_; }

private:
    void FollowLocalPlayer(const World& world, EntityId localControlSubject,
                           WorldPartitionRuntime& partition);
    void FollowPeers(const World* world, WorldPartitionRuntime& partition);
    void OfferInterest(NetSession& session, ReplicationRuntime& replication,
                       WorldPartitionRuntime& partition, NetStats* traffic);
    void LoadWhatWasGranted(const NetZoneScope& scope,
                            WorldPartitionRuntime& partition);

    // One peer wanting one zone, before the duplicates between the things it
    // drives are collapsed.
    struct Claim
    {
        std::uint32_t Peer = 0;
        ZoneId Zone;
    };

    // All reused rather than rebuilt: this runs every frame.
    std::vector<FocusSourceId> Held_;
    std::vector<FocusSourceId> Live_;
    std::vector<Claim> Claims_;
    std::vector<ZoneId> SourceZones_;
    std::vector<ZoneId> InterestZones_;
    std::vector<NetPeerZoneInterest> Interest_;
    std::vector<ZoneId> Streamed_;
    std::vector<ZoneId> Pinned_;
    std::vector<ZoneId> Wanted_;
};
