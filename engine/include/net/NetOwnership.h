#pragma once

#include <ecs/EntityId.h>
#include <net/NetSession.h>

#include <cstdint>
#include <vector>

class ClientPrediction;
class World;

//=============================================================================
// Network ownership, and local control
//
// Two facts that read alike and are not the same fact.
//
// NetOwner is the authority's record of whose commands may drive an entity. It
// replicates, it is per entity, and a peer can own more than one thing.
// Everything the authority derives from it -- which input source the entity
// reads, whose snapshot carries its owner-only fields -- is derived here and
// nowhere else. A NetOwner written by hand leaves those derivations describing
// the previous owner, and the symptom is a peer still steering what it no
// longer owns.
//
// Local control is which entity THIS machine drives, and at most one is. A
// listen server's own pawn is locally controlled and owned by no peer; a
// dedicated host owns every pawn and drives none; a client holds NetOwner
// records for every player's pawn and drives one. The two sets differ inside a
// single process, which is why they are not one component.
//=============================================================================

//-----------------------------------------------------------------------------
// Ownership, on the authority
//
// Peer zero is the authority. It is a value rather than the component's
// absence, because a snapshot carries values: "no longer yours" has to be a
// number a client can be told. So NetOwner is written, never removed.
//-----------------------------------------------------------------------------

// Makes `peer` the owner, installing everything that follows from it.
// Idempotent.
//
// A transfer is one call. The previous owner's derivations come off in the same
// call that installs the new owner's, so no frame exists in which two peers
// steer one entity -- which is what a game doing this by hand cannot promise,
// because it has to remember every place the answer is written down.
//
// Structural: adds and removes components, so not from inside a query.
void NetSetOwner(World& world, EntityId entity, PeerId peer);

// Hands the entity back to the authority: the owner becomes peer zero and the
// input reference goes.
void NetClearOwner(World& world, EntityId entity);

// Everything this peer owned, handed back, and the input source its commands
// were landing in, closed. This is the one un-possession nobody gets to ask
// for, and closing the source is not housekeeping: without it every peer that
// ever connects leaves an InputActionState behind for the life of the process.
void NetForgetOwnerPeer(World& world, PeerId peer);

// Whoever owns it, or an invalid PeerId for the authority.
[[nodiscard]] PeerId NetOwnerOf(const World& world, EntityId entity);

// Everything `peer` owns, appended to `out`. Walks the NetOwner column, so it
// costs the number of entities somebody drives rather than the size of the
// world.
//
// This is what replaces a game keeping its own peer-to-entity map beside the
// component that already says it -- and it is the map, not the component, that
// goes stale when ownership moves.
void NetOwnedBy(const World& world, PeerId peer, std::vector<EntityId>& out);

//-----------------------------------------------------------------------------
// LocalControlSubject
//
// The one entity this machine drives. One slot, because "at most one" is the
// invariant rather than a convention: two copies of this answer is how a body
// the player steers ends up beside a second one the camera is riding.
//
// Holds the id without keeping it alive, so a destroyed subject reads back as
// none and nothing has to remember to clear it.
//-----------------------------------------------------------------------------
struct LocalControlSubject
{
    EntityId Value;
};

[[nodiscard]] EntityId LocalControlSubjectOf(const World& world);

// Makes `entity` the one this machine drives, installing the per-machine facts
// that follow -- the look-control tag, and the prediction subject when a client
// is asking -- and removing them from whatever held them before, which is the
// half a hand-written possession always forgets.
//
// An invalid entity relinquishes without taking anything up, which is what a
// peer that has been un-possessed and a client whose session ended both are.
//
// `prediction` is null on a machine that predicts nothing: an authority does
// not predict its own pawn, it defines it.
//
// Structural.
void NetSetLocalControl(World& world, EntityId entity, ClientPrediction* prediction);

// Client side. Brings local control into agreement with the ownership the
// authority replicated.
//
// Called from the net pump once snapshots have been applied, which is both
// where the fact arrives and where structural work on arriving state already
// happens -- so every tick of the frame sees a settled answer and there is no
// ordering to declare.
//
// A walk of the NetOwner column rather than a Changed<NetOwner> filter. Changed
// is chunk-conservative: a chunk of pawns reports every one of them when any
// one is written, leaving the comparison against what this machine already
// drives to be made anyway, and that comparison is the whole work. The walk
// costs the player count.
void NetReconcileLocalControl(World& world, PeerId self, ClientPrediction& prediction);
