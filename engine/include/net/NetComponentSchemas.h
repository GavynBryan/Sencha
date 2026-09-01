#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <core/text/InlineString.h>
#include <net/NetParticipantIdentity.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnPrefab.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape and replication policy for the net components.
//
// Registration and the serializers include this; the systems that read these
// components do not.
//=============================================================================

template <>
struct TypeSchema<NetOwner>
{
    static constexpr std::string_view Name = "NetOwner";
    // Ownership itself travels: a client cannot know which entity is its own
    // until the authority says so, and everything owner-only keys off it.
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("peer", &NetOwner::Peer),
        };
    }
};

template <>
struct TypeSchema<NetDrivenBy>
{
    static constexpr std::string_view Name = "NetDrivenBy";
    // A client cannot tell which of the entities it holds is the one its own
    // input reaches until the authority says so.
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("peer", &NetDrivenBy::Peer),
        };
    }
};

template <>
struct TypeSchema<NetSpawnPrefab>
{
    static constexpr std::string_view Name = "NetSpawnPrefab";
    // How a client builds the entity in the first place, so it has to arrive in
    // the same snapshot that first mentions it.
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("scene", &NetSpawnPrefab::Scene),
        };
    }
};

template <>
struct TypeSchema<NetParticipantIdentity>
{
    static constexpr std::string_view Name = "NetParticipantIdentity";
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("peer", &NetParticipantIdentity::Peer),
        };
    }
};
