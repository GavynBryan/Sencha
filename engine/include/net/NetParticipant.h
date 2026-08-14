#pragma once

#include <ecs/EntityId.h>
#include <net/NetPlayer.h>
#include <net/NetSession.h>

#include <functional>

class World;

//=============================================================================
// Participant lifecycle
//
// Admission is what makes somebody a participant -- a peer joining, a local
// player entering play, a bot being added. The engine runs the sequence: create
// the player, let the game compose it, ask for a body, bind what comes back.
// The game answers only the two questions it alone can.
//
// Deliberately not driven by control sources. A cutscene or a script driving an
// actor allocates a source and admits nobody, so a source appearing is not an
// event that produces a participant.
//=============================================================================
struct NetParticipantPolicies
{
    // The game's own components on a player the engine has just created: score,
    // team, name, whatever a participant is in this game. Absent is fine -- a
    // participant with nothing but its identity is a legitimate thing to be.
    std::function<void(World&, EntityId player)> BuildPlayer;

    // A body for this player, or an invalid id for none.
    //
    // None is an ordinary answer rather than a failure: a spectator, a player
    // waiting to respawn, and a session whose content has not finished loading
    // all give it. The engine does not ask again on its own, which is what
    // keeps those cases distinguishable -- see NetRequestPlayerBody.
    std::function<EntityId(World&, EntityId player)> ProvideBody;

    // Whether a departing participant's body is destroyed. Absent means yes,
    // which is what a session pawn wants. A game that leaves bodies standing
    // after a disconnect says so here rather than by working around the reap.
    std::function<bool(World&, EntityId player, EntityId body)> ReapBody;
};

// Creates a participant driven by `peer`, composes it, and asks for a body.
// An invalid peer admits one with no peer behind it: a player standing alone,
// or a bot. Returns the player, or an invalid id if the world tracks none.
//
// Structural.
EntityId NetAdmitParticipant(World& world, PeerId peer,
                             const NetParticipantPolicies& policies,
                             NetParticipantPresence presence
                             = NetParticipantPresence::Simulated);

// Asks for a body for a player that has none, and binds what comes back:
// replicated, owned by the participant, and driven by them.
//
// Does nothing for a player that already has a living body, because a second
// body is not something a caller can mean.
//
// This is the respawn seam. When a body dies, when it is safe to come back, and
// whether to come back at all are the game's to decide; nothing polls on its
// behalf, which is what keeps "waiting to respawn" and "spectating for good"
// from being the same state.
//
// Structural.
EntityId NetRequestPlayerBody(World& world, EntityId player,
                              const NetParticipantPolicies& policies);

// Releases what the participant was driving, reaps its body if the policy
// allows, and destroys the player. Returns the body it destroyed, or an invalid
// id when there was none or the policy kept it -- so a caller can report what a
// departure actually cost without having to work it out again.
//
// What it was driving is let go of and never destroyed. Somebody quitting at
// the controls of a turret does not own the turret, and the difference between
// those two facts is the whole reason they are separate.
//
// Structural.
EntityId NetRetireParticipant(World& world, EntityId player,
                              const NetParticipantPolicies& policies);
