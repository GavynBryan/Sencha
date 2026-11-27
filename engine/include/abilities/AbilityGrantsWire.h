#pragma once

#include <abilities/AbilityGrants.h>
#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// A grant's id, the ability's wire key, its entry key's NameWireKey, its kind.
struct AbilityGrantWireWords
{
    std::uint32_t Values[4] = {};
};

struct AbilityGrantWire
{
    AbilityGrantWireWords Words;
    std::uint64_t Source = 0;
};

// NextId travels so the owner's grants keep the authority's ids.
struct AbilityGrantsWireHead
{
    std::uint32_t NextId = 1;
};

template <>
struct PackedScalarGroup<AbilityGrantWireWords>
{
    static constexpr std::size_t Count = 4;
    using Component = std::uint32_t;
};

template <>
struct TypeSchema<AbilityGrantWire>
{
    static constexpr std::string_view Name = "ability_grant_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("words", &AbilityGrantWire::Words).OwnerOnly(),
                           MakeField("source", &AbilityGrantWire::Source).OwnerOnly() };
    }
};

template <>
struct TypeSchema<AbilityGrantsWireHead>
{
    static constexpr std::string_view Name = "ability_grants_wire_head";
    static auto Fields() { return std::tuple{ MakeField("next_id", &AbilityGrantsWireHead::NextId).OwnerOnly() }; }
};

// An actor's grants go to its owner alone, who predicts with them, in grant order.
template <>
struct ReplicationCodec<AbilityGrants>
{
    using Wire = AbilityGrantsWireHead;
    using Lists = std::tuple<ReplicatedListOf<AbilityGrantWire, kAbilityGrantCapacity>>;
    static constexpr std::string_view ListNames[] = { "grants" };
    static void ToWire(const ReplicationWireContext& context, const AbilityGrants& grants, ReplicationImage& wire);
    static void FromWire(const ReplicationWireContext& context, const ReplicationImage& wire, AbilityGrants& grants);
};
