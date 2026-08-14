#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <input/InputActionSource.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSession.h>

#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

class World;

//=============================================================================
// The player
//
// A participant in the session, and the thing peers, pawns, and ownership are
// all about. It outlives bodies: dying, respawning, swapping character, and
// climbing into a turret change what a player drives, never whether it exists.
//
// Deliberately not the pawn. A pawn standing in for the player has nowhere to
// keep a score that survives death, no way to represent a peer between
// respawns, and no answer for a player whose body is parked while they drive
// something else -- which is why the fact ended up recorded on the turret.
//
// The engine owns the control binding and the two links below and nothing more.
// Score, team, name, respawn timer, and inventory are the game's own components
// on the same entity, which is what keeps this from becoming the object that
// owns everything about a player.
//=============================================================================

//-----------------------------------------------------------------------------
// NetPlayer
//
// Which peer this player is, or the authority for one with no peer behind it --
// a player standing alone, the host's own, a bot.
//
// Replicated, because a client cannot pick its own player out of the session's
// or draw a scoreboard without being told who each one is. Everything else
// about a player either travels as its own component or does not travel.
//-----------------------------------------------------------------------------
struct NetPlayer
{
    std::uint32_t Peer = kNetAuthorityPeer;
};

static_assert(std::is_trivially_copyable_v<NetPlayer>,
              "NetPlayer must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(NetPlayer, "sencha.net_player");

template <>
struct TypeSchema<NetPlayer>
{
    static constexpr std::string_view Name = "NetPlayer";
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("peer", &NetPlayer::Peer),
        };
    }
};

//-----------------------------------------------------------------------------
// NetPlayerControl
//
// What this player drives, and what belongs to it.
//
// Two references, because one cannot say what a player in a turret is: the body
// waiting outside is still theirs, and the turret is what their keys reach.
// Collapsing them is how the body is lost the moment they take something else,
// and how a departing driver takes the turret with them.
//
// The authority's record and nobody else's. It holds runtime entity handles,
// which mean nothing on another machine; what a client needs -- which entity it
// drives -- reaches it as NetDrivenBy on that entity instead.
//
// No schema: it neither replicates nor cooks, so all it needs is an identity
// its storage column can be found under.
//-----------------------------------------------------------------------------
struct NetPlayerControl
{
    // Where this player's input comes from. Allocated from the shared source
    // space, never a peer number -- see NetSourceForPeer.
    InputActionSourceId Source = kLocalInputActionSource;

    // The entity that belongs to this player. The lifetime link: reaping what a
    // departing player leaves behind means this one and never the subject.
    EntityId Body;

    // The entity currently receiving this player's input. The body most of the
    // time, and the turret, vehicle, or spectator camera when it is not.
    EntityId ControlSubject;
};

static_assert(std::is_trivially_copyable_v<NetPlayerControl>,
              "NetPlayerControl must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(NetPlayerControl, "sencha.net_player_control");

//-----------------------------------------------------------------------------
// Finding one
//-----------------------------------------------------------------------------

// The player driven by this peer, or an invalid id. A walk of the NetPlayer
// column, so it costs the number of participants rather than the size of the
// world.
[[nodiscard]] EntityId NetPlayerForPeer(const World& world, PeerId peer);

//-----------------------------------------------------------------------------
// Admission and retirement
//
// Creating a player is what admission means, and only the authority does it. A
// client receives players as replicated state; building one locally as well is
// how a machine ends up holding two representations of the same person.
//-----------------------------------------------------------------------------

// Admits a participant driven by `peer` and returns its player, or the existing
// one when that peer already has a player. An invalid peer admits a participant
// with no peer behind it, which is a player standing alone or a bot.
//
// `partition` is where the entity is created. Zero is the persistent partition,
// which is where a player belongs: a participant that vanished because the room
// it was created in unloaded would take the session's record of them with it.
//
// Structural.
EntityId NetAdmitPlayer(World& world, PeerId peer, std::uint16_t partition = 0);

//-----------------------------------------------------------------------------
// Possession
//-----------------------------------------------------------------------------

// Binds this player's input to `subject`, taking it off whatever the player was
// driving before. An invalid subject relinquishes without taking anything up,
// which is what a player who has just died and one handing a turret back both
// are.
//
// Ownership is deliberately absent. A vehicle can be driven by one player and
// owned by nobody, so installing ownership here would make the two facts
// incapable of differing -- and it is exactly their difference that says a
// departing driver does not take the vehicle with them.
//
// Structural.
void NetPossess(World& world, EntityId player, EntityId subject);

// Releases what the player was driving and destroys the player.
//
// Deliberately not its body, and deliberately not its subject: a player that
// disconnects while driving a turret does not own the turret, and what happens
// to the body they left behind is a decision this cannot make for a game.
//
// Structural.
void NetRetirePlayer(World& world, EntityId player);
