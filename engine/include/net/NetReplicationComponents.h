#pragma once

#include <core/identity/StrongId.h>
#include <ecs/ComponentTypeId.h>

#include <cstdint>
#include <type_traits>

//=============================================================================
// Replication components
//
// What the replication writer reads off an entity: whether it travels at all,
// who it belongs to, and whose input currently reaches it. All plain data, and
// all absent from the overwhelming majority of entities -- a level's worth of
// authored geometry carries none of them, which is what keeps the writer's work
// proportional to what actually moves rather than to what exists.
//=============================================================================

//-----------------------------------------------------------------------------
// NetReplicated
//
// Opts one entity into replication. Deliberately per-entity rather than implied
// by carrying a replicated component: nearly everything in a level has a
// transform, and almost none of it is worth a byte per tick.
//
// The authority reads this. On a client it means nothing, because a client
// sends no state.
//-----------------------------------------------------------------------------
struct NetReplicated
{
};

static_assert(std::is_empty_v<NetReplicated>,
              "NetReplicated is a tag: presence is its whole meaning");

SENCHA_DECLARE_COMPONENT_TYPE(NetReplicated, "sencha.net_replicated");

//-----------------------------------------------------------------------------
// NetOwner
//
// Which peer's inputs drive this entity, and therefore which peer sees its
// owner-only fields. Absent, or present and invalid, means the authority owns
// it -- an authored door and a server-driven turret both simply have no owner.
//
// Not replicated as a value the way state is: a client learns it owns something
// through the snapshot that carries this component, so it is in the replicated
// table like any other data.
//-----------------------------------------------------------------------------
// No peer. PeerId mints from one, so zero can never name one, and an entity
// nobody owns and an entity the authority owns are deliberately the same thing.
//
// Write this rather than removing the component. A snapshot carries values, and
// a client reads the owner every tick to decide what it may be shown, so
// "no longer yours" has to be a number rather than an absence.
inline constexpr std::uint32_t kNetAuthorityPeer = 0;

struct NetOwner
{
    // Deliberately the raw peer number rather than PeerId: components are data,
    // and the wire codec addresses plain scalars. The session converts.
    std::uint32_t Peer = 0;
};

static_assert(std::is_trivially_copyable_v<NetOwner>,
              "NetOwner must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(NetOwner, "sencha.net_owner");

//-----------------------------------------------------------------------------
// NetDrivenBy
//
// Which peer's input currently reaches this entity, and a client's whole answer
// to which of the entities it holds is the one it drives.
//
// Carries the peer rather than the player entity or the input source, because a
// peer number is a name both machines already agree on: an entity handle means
// nothing across the wire, and a source id means something different on each
// machine -- on the authority it names a peer's command buffer, while on that
// peer's own machine the same entity reads the devices on the desk.
//
// Distinct from NetOwner, which says who a thing belongs to. The two agree for a
// player's own pawn and part company the moment somebody drives a vehicle they
// do not own -- which is also what decides that a driver disconnecting does not
// take the vehicle with them.
//-----------------------------------------------------------------------------
struct NetDrivenBy
{
    std::uint32_t Peer = kNetAuthorityPeer;
};

static_assert(std::is_trivially_copyable_v<NetDrivenBy>,
              "NetDrivenBy must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(NetDrivenBy, "sencha.net_driven_by");
