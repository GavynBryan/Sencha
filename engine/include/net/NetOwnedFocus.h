#pragma once

#include <ecs/EntityId.h>
#include <net/NetSession.h>
#include <net/ReplicationRuntime.h>
#include <zone/ZoneDemand.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class World;
class WorldPartitionRuntime;

//=============================================================================
// Streaming focus for the entities peers drive
//
// An authority simulates on behalf of everyone connected to it, so what it has
// to keep resident is not one neighborhood but all of theirs together. The zone
// layer already merges several focus sources into one demand set and never
// learns what any of them are; this is the piece that fills that set in on a
// machine holding a session, and the only place the word "peer" and the word
// "zone" appear together.
//=============================================================================

// The high bit marks a source this mechanism minted, keeping it clear of
// kPrimaryFocusSource and of any id a game hands out itself. Slot indices are
// bounded by the world's entity table and do not approach it.
inline constexpr std::uint32_t kNetFocusSourceBit = 0x8000'0000u;

// Which source an owned entity streams under.
//
// Keyed by entity rather than by peer, because a peer can drive more than one
// thing at once: somebody in a turret still has a body sitting in it, and the
// zone under that body has to stay simulated for them to get back into it. Per
// entity that is exactly the union the demand merge already computes. Per peer
// it would need a rule for which of a peer's entities counts, and every answer
// to that question is arbitrary.
[[nodiscard]] FocusSourceId NetFocusSourceFor(EntityId entity);

// Keeps a partition's focus sources in agreement with what peers own.
//
// It holds the ids it minted, because dropping a source once nobody drives that
// entity is half the job. Without it every player who ever connects leaves
// their neighborhood resident behind them, and the symptom is a server whose
// memory only ever goes up.
class NetOwnedFocus
{
public:
    // Once per frame, before WorldPartitionRuntime::Update and beside the focus
    // a locally controlled pawn already sets. Costs the number of entities
    // somebody is driving.
    //
    // The role is a parameter rather than something read back out of the world
    // because getting it wrong is silent: NetOwner replicates, so a client
    // walking the same column would stream the ground under every other player
    // as well as its own. Anything that is not a host releases what it held.
    void Update(const World& world, NetSessionRole role,
                WorldPartitionRuntime& partition);

    // The sources held right now, ascending by id. For diagnostics and tests;
    // the partition is the one that acts on them.
    [[nodiscard]] std::span<const FocusSourceId> Held() const { return Held_; }

    // What each peer should be holding open: the union of the neighborhoods of
    // everything that peer drives, ready to hand to
    // ReplicationRuntime::PublishZoneScope. Ascending by peer, and each peer's
    // zones ascending and deduplicated, which is what that call requires.
    //
    // The spans point into storage this object owns and the next Update
    // rewrites, so it is read within the frame that produced it.
    //
    // Computed from each source's focus as it stood entering this frame, which
    // is one frame behind a player who crosses a boundary during it. That costs
    // nothing: with any hop count at all the room being entered was already a
    // neighbor, so it was already granted and already acked, and the lag is only
    // ever in when the room behind stops being offered.
    [[nodiscard]] std::span<const NetPeerZoneInterest> Interest() const
    {
        return Interest_;
    }

private:
    // One peer wanting one zone, before the duplicates between the things it
    // drives are collapsed.
    struct Claim
    {
        std::uint32_t Peer = 0;
        ZoneId Zone;
    };

    std::vector<FocusSourceId> Held_;
    // All reused rather than rebuilt: this runs every frame.
    std::vector<FocusSourceId> Live_;
    std::vector<Claim> Claims_;
    std::vector<ZoneId> SourceZones_;
    std::vector<ZoneId> InterestZones_;
    std::vector<NetPeerZoneInterest> Interest_;
};
