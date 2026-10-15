#pragma once

#include <abilities/AbilityGrants.h>
#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// Per grant: its id, the ability's wire key, its entry key's NameWireKey, its kind.
struct AbilityGrantsWireWords
{
    std::uint32_t Values[4 * kAbilityGrantCapacity] = {};
};

// Per grant: the effect holding it, as a wire entity.
struct AbilityGrantsWireEffects
{
    std::uint64_t Values[kAbilityGrantCapacity] = {};
};

// An actor's grants go to its owner alone, who predicts with them; NextId too, so
// the owner's grants keep the authority's ids.
struct AbilityGrantsWire
{
    AbilityGrantsWireWords Words;
    AbilityGrantsWireEffects Effects;
    std::uint32_t NextId = 1;
};

template <>
struct PackedScalarGroup<AbilityGrantsWireWords>
{
    static constexpr std::size_t Count = 4 * kAbilityGrantCapacity;
    using Component = std::uint32_t;
};

template <>
struct PackedScalarGroup<AbilityGrantsWireEffects>
{
    static constexpr std::size_t Count = kAbilityGrantCapacity;
    using Component = std::uint64_t;
};

template <>
struct TypeSchema<AbilityGrantsWire>
{
    static constexpr std::string_view Name = "ability_grants_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("words", &AbilityGrantsWire::Words).OwnerOnly(),
                           MakeField("effects", &AbilityGrantsWire::Effects).OwnerOnly(),
                           MakeField("next_id", &AbilityGrantsWire::NextId).OwnerOnly() };
    }
};

template <>
struct ReplicationCodec<AbilityGrants>
{
    using Wire = AbilityGrantsWire;
    static void ToWire(const ReplicationWireContext& context, const AbilityGrants& grants, AbilityGrantsWire& wire);
    static void FromWire(const ReplicationWireContext& context, const AbilityGrantsWire& wire, AbilityGrants& grants);
};
