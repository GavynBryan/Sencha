#pragma once

#include <ecs/EntityId.h>
#include <net/NetSession.h>

#include <vector>

class World;

//=============================================================================
// Network ownership
//
// Two facts that read alike and are not the same fact.
//
// NetOwner is the authority's record of which peer owns an entity. It is per
// entity, and a peer can own more than one thing. Snapshot field visibility and
// command authorization read this one value directly. It is deliberately not
// control: a participant can own a body while driving a turret.
//
// Local control and peer input-source lifetime have their own owners. Keeping
// this API about ownership makes it impossible to confuse those facts merely
// because all three participate in a session projection.
//=============================================================================

//-----------------------------------------------------------------------------
// Ownership, on the authority
//
// Peer zero is the authority. It is a value rather than the component's
// absence, because a snapshot carries values: "no longer yours" has to be a
// number a client can be told. So NetOwner is written, never removed.
//-----------------------------------------------------------------------------

// Makes `peer` the owner. Idempotent.
//
// A transfer is one call. The previous owner's derivations come off in the same
// call that installs the new owner's, so no frame exists in which two peers
// steer one entity -- which is what a game doing this by hand cannot promise,
// because it has to remember every place the answer is written down.
//
// Structural: adds and removes components, so not from inside a query.
void NetSetOwner(World& world, EntityId entity, PeerId peer);

// Hands the entity back to the authority: the owner becomes peer zero. Whoever
// is at the controls is unchanged.
void NetClearOwner(World& world, EntityId entity);

// Everything this peer owned is handed back. Peer input-source teardown is a
// separate mechanism coordinated by SessionParticipantProjection.
void NetForgetOwnerPeer(World& world, PeerId peer);

// Whoever owns it, or an invalid PeerId for the authority.
[[nodiscard]] PeerId NetOwnerOf(const World& world, EntityId entity);

// Everything `peer` owns, replacing whatever `out` held. Walks the NetOwner
// column, so it costs the number of entities somebody owns rather than the
// size of the world.
//
// Fills rather than appends, and the difference is not stylistic: the natural
// use is one reused vector around a loop over peers, and an appending version
// answers that with every earlier peer's entities still in it. That reads as
// one peer owning another's pawn, which is alarming, wrong, and invisible
// until a session has two peers in it.
//
// This is what replaces a game keeping its own peer-to-entity map beside the
// component that already says it -- and it is the map, not the component, that
// goes stale when ownership moves.
void NetOwnedBy(const World& world, PeerId peer, std::vector<EntityId>& out);
