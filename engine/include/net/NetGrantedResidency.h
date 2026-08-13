#pragma once

#include <net/NetZoneScope.h>
#include <zone/ZoneParticipation.h>

#include <span>
#include <vector>

class WorldPartitionRuntime;

//=============================================================================
// Loading what the authority granted
//
// A client streams around its own pawn, and in steady state that agrees with
// what the authority granted it: both are the same pure function of the same
// replicated position. The cases that matter are the ones where it does not.
//
// A player who travels -- a teleport, a scripted move, a spawn somewhere they
// have never been -- has an authority that already knows where they are and a
// client that does not yet, because the fact is arriving in a zone the client
// has no reason to load. Left to local policy alone that is a deadlock in the
// making: the authority withholds the room until the client acks, and the
// client never loads the room it was never told to want.
//
// So a grant is a floor on the client's own demand. Its policy still runs and
// still prefetches neighbours for presentation; this only makes sure the rooms
// the authority is waiting on are among the ones being loaded.
//
// One thing this does not fix, because it cannot and does not need to: a pin
// only holds a zone open on a machine whose focus already resolves somewhere,
// since demand around no focus at all is demand for nothing. That is not a
// hole. Grants exist only for a peer the authority computes interest for, which
// means a peer that owns something, which means a client that has been sent the
// thing it owns -- and a client with a pawn has a focus.
//=============================================================================

// Keeps a partition's pins in agreement with a client's zone scope.
//
// Holds what it pinned, because a pin nothing takes back is a room the client
// keeps resident for the rest of the session. Pins are last-writer-wins per
// zone, so this must not be pointed at a partition whose pins a game also sets
// by hand -- and a game that needs both wants a participation lease instead.
class NetGrantedResidency
{
public:
    // Once per frame, before WorldPartitionRuntime::Update. Cheap when nothing
    // changed, which is every frame a player is not crossing anything.
    //
    // `minimum` is the floor a granted zone is held at. Full by default: a room
    // the authority is sending state for is one this machine has to simulate
    // and draw, and a dormant pin would leave a client holding entities it
    // never ticks.
    void Update(const NetZoneScope& scope, WorldPartitionRuntime& partition,
                ZoneParticipation minimum = ZoneParticipation{ .Visible = true,
                                                               .Physics = true,
                                                               .Logic = true,
                                                               .Audio = true });

    [[nodiscard]] std::span<const ZoneId> Pinned() const { return Pinned_; }

private:
    // Ascending by zone id.
    std::vector<ZoneId> Pinned_;
    std::vector<ZoneId> Wanted_;
};
