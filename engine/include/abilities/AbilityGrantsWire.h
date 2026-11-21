#pragma once

#include <abilities/AbilityGrants.h>
#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

inline constexpr std::size_t kAbilityGrantsPerWirePage = 4;
inline constexpr std::size_t kAbilityGrantWirePages = 16;
static_assert(kAbilityGrantsPerWirePage * kAbilityGrantWirePages == kAbilityGrantCapacity,
              "an actor holds what its owner can be sent");

// Per grant: its id, the ability's wire key, its entry key's NameWireKey, its kind.
struct AbilityGrantsWireWords
{
    std::uint32_t Values[4 * kAbilityGrantsPerWirePage] = {};
};

struct AbilityGrantsWireSources
{
    std::uint64_t Values[kAbilityGrantsPerWirePage] = {};
};

// Grants in order, a page each; a page no grant reaches stays at its default and
// is not sent.
struct AbilityGrantsWirePage
{
    AbilityGrantsWireWords Words;
    AbilityGrantsWireSources Sources;
};

// An actor's grants go to its owner alone, who predicts with them; NextId too, so
// the owner's grants keep the authority's ids.
struct AbilityGrantsWire
{
    std::uint32_t NextId = 1;
    AbilityGrantsWirePage P0, P1, P2, P3, P4, P5, P6, P7, P8, P9, P10, P11, P12, P13, P14, P15;
};

template <>
struct PackedScalarGroup<AbilityGrantsWireWords>
{
    static constexpr std::size_t Count = 4 * kAbilityGrantsPerWirePage;
    using Component = std::uint32_t;
};

template <>
struct PackedScalarGroup<AbilityGrantsWireSources>
{
    static constexpr std::size_t Count = kAbilityGrantsPerWirePage;
    using Component = std::uint64_t;
};

template <>
struct TypeSchema<AbilityGrantsWirePage>
{
    static constexpr std::string_view Name = "ability_grants_wire_page";
    static auto Fields()
    {
        return std::tuple{ MakeField("words", &AbilityGrantsWirePage::Words).OwnerOnly(),
                           MakeField("sources", &AbilityGrantsWirePage::Sources).OwnerOnly() };
    }
};

template <>
struct TypeSchema<AbilityGrantsWire>
{
    static constexpr std::string_view Name = "ability_grants_wire";
    static auto Fields()
    {
        using W = AbilityGrantsWire;
        return std::tuple{ MakeField("next_id", &W::NextId).OwnerOnly(),
                           MakeField("p0", &W::P0),   MakeField("p1", &W::P1),   MakeField("p2", &W::P2),
                           MakeField("p3", &W::P3),   MakeField("p4", &W::P4),   MakeField("p5", &W::P5),
                           MakeField("p6", &W::P6),   MakeField("p7", &W::P7),   MakeField("p8", &W::P8),
                           MakeField("p9", &W::P9),   MakeField("p10", &W::P10), MakeField("p11", &W::P11),
                           MakeField("p12", &W::P12), MakeField("p13", &W::P13), MakeField("p14", &W::P14),
                           MakeField("p15", &W::P15) };
    }
};

template <>
struct ReplicationCodec<AbilityGrants>
{
    using Wire = AbilityGrantsWire;
    static void ToWire(const ReplicationWireContext& context, const AbilityGrants& grants, AbilityGrantsWire& wire);
    static void FromWire(const ReplicationWireContext& context, const AbilityGrantsWire& wire, AbilityGrants& grants);
};
