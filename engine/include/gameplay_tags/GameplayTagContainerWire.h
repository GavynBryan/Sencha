#pragma once

#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// How many of an actor's tags it can show to everyone at once.
inline constexpr std::size_t kGameplayTagSharedCapacity = 8;

// Tags only the owner is sent, by wire key, and their stack counts.
struct GameplayTagWireOwned
{
    std::uint32_t Keys[GameplayTagContainer::Capacity] = {};
    std::uint32_t Counts[GameplayTagContainer::Capacity] = {};
};

// Tags declared replicated to all, by wire key, which everyone is sent.
struct GameplayTagWireShared
{
    std::uint32_t Keys[kGameplayTagSharedCapacity] = {};
};

struct GameplayTagContainerWire
{
    GameplayTagWireOwned Owned;
    GameplayTagWireShared Shared;
};

template <>
struct PackedScalarGroup<GameplayTagWireOwned>
{
    static constexpr std::size_t Count = 2 * GameplayTagContainer::Capacity;
    using Component = std::uint32_t;
};

template <>
struct PackedScalarGroup<GameplayTagWireShared>
{
    static constexpr std::size_t Count = kGameplayTagSharedCapacity;
    using Component = std::uint32_t;
};

template <>
struct TypeSchema<GameplayTagContainerWire>
{
    static constexpr std::string_view Name = "gameplay_tag_container_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("owned", &GameplayTagContainerWire::Owned).OwnerOnly(),
                           MakeField("shared", &GameplayTagContainerWire::Shared) };
    }
};

// The two halves split by the tag, on every machine alike, so what a machine
// was never sent is never in what it sends back as its own image.
template <>
struct ReplicationCodec<GameplayTagContainer>
{
    using Wire = GameplayTagContainerWire;
    static void ToWire(const ReplicationWireContext& context, const GameplayTagContainer& tags,
                       GameplayTagContainerWire& wire);
    static void FromWire(const ReplicationWireContext& context, const GameplayTagContainerWire& wire,
                         GameplayTagContainer& tags);
};
