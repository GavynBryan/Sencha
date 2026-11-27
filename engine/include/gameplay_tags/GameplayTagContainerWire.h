#pragma once

#include <core/metadata/TypeSchema.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// A tag only the owner is sent, by wire key, and its stack count.
struct GameplayTagOwnedWire
{
    std::uint32_t Key = 0;
    std::uint32_t Count = 0;
};

// A tag declared replicated to all, by wire key, which everyone is sent.
struct GameplayTagSharedWire
{
    std::uint32_t Key = 0;
};

template <>
struct TypeSchema<GameplayTagOwnedWire>
{
    static constexpr std::string_view Name = "gameplay_tag_owned_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("key", &GameplayTagOwnedWire::Key).OwnerOnly(),
                           MakeField("count", &GameplayTagOwnedWire::Count).OwnerOnly() };
    }
};

template <>
struct TypeSchema<GameplayTagSharedWire>
{
    static constexpr std::string_view Name = "gameplay_tag_shared_wire";
    static auto Fields() { return std::tuple{ MakeField("key", &GameplayTagSharedWire::Key) }; }
};

// The two halves split by the tag, on every machine alike, so what a machine
// was never sent is never in what it sends back as its own image. Each half is
// in wire-key order, which every machine computes alike from the same tags.
template <>
struct ReplicationCodec<GameplayTagContainer>
{
    using Wire = ReplicationNoHead;
    using Lists = std::tuple<ReplicatedListOf<GameplayTagOwnedWire, GameplayTagContainer::Capacity>,
                             ReplicatedListOf<GameplayTagSharedWire, GameplayTagContainer::Capacity>>;
    static constexpr std::string_view ListNames[] = { "owned", "shared" };
    static void ToWire(const ReplicationWireContext& context, const GameplayTagContainer& tags, ReplicationImage& wire);
    static void FromWire(const ReplicationWireContext& context, const ReplicationImage& wire,
                         GameplayTagContainer& tags);
};
