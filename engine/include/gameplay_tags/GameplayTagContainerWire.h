#pragma once

#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// Every tag a container can hold can be shown to everyone, so none is ever
// dropped. Split into runs so an actor showing only a few sends only the first.
inline constexpr std::size_t kGameplayTagSharedRunLength = 8;
inline constexpr std::size_t kGameplayTagSharedCapacity = GameplayTagContainer::Capacity;

// Tags only the owner is sent, by wire key, and their stack counts.
struct GameplayTagWireOwned
{
    std::uint32_t Keys[GameplayTagContainer::Capacity] = {};
    std::uint32_t Counts[GameplayTagContainer::Capacity] = {};
};

struct GameplayTagWireSharedRun
{
    std::uint32_t Keys[kGameplayTagSharedRunLength] = {};
};

// Tags declared replicated to all, by wire key, which everyone is sent, filled
// from the first run.
struct GameplayTagWireShared
{
    GameplayTagWireSharedRun R0, R1, R2, R3;

    [[nodiscard]] std::uint32_t& Key(std::size_t index);
    [[nodiscard]] std::uint32_t Key(std::size_t index) const;
};

static_assert(4 * kGameplayTagSharedRunLength == kGameplayTagSharedCapacity,
              "GameplayTagWireShared names one member per run.");

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
struct PackedScalarGroup<GameplayTagWireSharedRun>
{
    static constexpr std::size_t Count = kGameplayTagSharedRunLength;
    using Component = std::uint32_t;
};

template <>
struct TypeSchema<GameplayTagWireShared>
{
    static constexpr std::string_view Name = "gameplay_tag_wire_shared";
    static auto Fields()
    {
        return std::tuple{ MakeField("r0", &GameplayTagWireShared::R0), MakeField("r1", &GameplayTagWireShared::R1),
                           MakeField("r2", &GameplayTagWireShared::R2), MakeField("r3", &GameplayTagWireShared::R3) };
    }
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
