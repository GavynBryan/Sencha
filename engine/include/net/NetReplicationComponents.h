#pragma once

#include <core/identity/StrongId.h>
#include <ecs/ComponentTypeId.h>

#include <cstdint>
#include <type_traits>

//=============================================================================
// Replication components
//
// The two facts the replication writer reads off an entity: whether it travels
// at all, and who it belongs to. Both are plain data, and both are absent from
// the overwhelming majority of entities -- a level's worth of authored geometry
// carries neither, which is what keeps the writer's work proportional to what
// actually moves rather than to what exists.
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
struct NetOwner
{
    // Deliberately the raw peer number rather than PeerId: components are data,
    // and the wire codec addresses plain scalars. The session converts.
    std::uint32_t Peer = 0;
};

static_assert(std::is_trivially_copyable_v<NetOwner>,
              "NetOwner must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(NetOwner, "sencha.net_owner");

template <>
struct TypeSchema<NetOwner>
{
    static constexpr std::string_view Name = "NetOwner";

    static auto Fields()
    {
        return std::tuple{
            MakeField("peer", &NetOwner::Peer),
        };
    }
};
