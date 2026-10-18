#include <gtest/gtest.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagContainerWire.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationCodec.h>
#include <net/ReplicationWireContext.h>

#include <algorithm>
#include <bit>
#include <format>
#include <string>
#include <vector>

namespace
{
    // One machine's tag names. Wire keys come from the names, so two of these
    // agree on every key while numbering their tags in different orders.
    struct TagMachine
    {
        TagMachine(std::size_t shared, std::size_t owned, bool reversed = false)
        {
            GameplayTagRegistry& tags = Entities.AddResource<GameplayTagRegistry>();
            std::vector<std::string> names;
            for (std::size_t i = 0; i < shared; ++i)
                names.push_back(std::format("Shared.T{}", i));
            for (std::size_t i = 0; i < owned; ++i)
                names.push_back(std::format("Owned.T{}", i));
            if (reversed)
                std::reverse(names.begin(), names.end());
            for (const std::string& name : names)
                if (const std::optional<GameplayTagId> id = tags.RegisterTag(name);
                    id.has_value() && name.starts_with("Shared."))
                    tags.SetReplicatedToAll(*id, true);
        }

        [[nodiscard]] GameplayTagId Find(std::string_view name) const
        {
            return Entities.TryGetResource<GameplayTagRegistry>()->FindTag(name);
        }

        [[nodiscard]] ReplicationWireContext Context() const { return { .Entities = &Entities, .Entity = {} }; }

        World Entities;
    };

    GameplayTagContainer Holding(const TagMachine& machine, std::size_t shared, std::size_t owned)
    {
        GameplayTagContainer tags;
        for (std::size_t i = 0; i < shared; ++i)
            EXPECT_TRUE(tags.Grant(machine.Find(std::format("Shared.T{}", i))));
        for (std::size_t i = 0; i < owned; ++i)
            EXPECT_TRUE(tags.Grant(machine.Find(std::format("Owned.T{}", i)), static_cast<std::uint16_t>(i + 2)));
        return tags;
    }

    // What a peer that does not own the actor would be sent on first sight.
    std::uint64_t RunsSentToEveryone(const ReplicatedComponent& component, const GameplayTagContainerWire& wire)
    {
        const std::span<const std::byte> image{ reinterpret_cast<const std::byte*>(&wire), sizeof(wire) };
        return ReplicationChangedFields(component, image, component.WireDefault)
             & ReplicationVisibleFields(component, false);
    }
}

TEST(GameplayTagContainerWire, EveryTagAContainerCanHoldReachesEveryone)
{
    const std::size_t all = GameplayTagContainer::Capacity;
    const TagMachine authority(all, 0);
    const TagMachine client(all, 0, true);

    GameplayTagContainerWire wire;
    ReplicationCodec<GameplayTagContainer>::ToWire(authority.Context(), Holding(authority, all, 0), wire);
    GameplayTagContainer received;
    ReplicationCodec<GameplayTagContainer>::FromWire(client.Context(), wire, received);

    ASSERT_EQ(received.Size(), all);
    for (std::size_t i = 0; i < all; ++i)
        EXPECT_TRUE(received.HasExact(client.Find(std::format("Shared.T{}", i)))) << "Shared.T" << i;
}

TEST(GameplayTagContainerWire, OwnedAndSharedHalvesRoundTripTogether)
{
    const TagMachine authority(20, 12);
    const TagMachine client(20, 12, true);

    GameplayTagContainerWire wire;
    ReplicationCodec<GameplayTagContainer>::ToWire(authority.Context(), Holding(authority, 20, 12), wire);
    GameplayTagContainer received;
    ReplicationCodec<GameplayTagContainer>::FromWire(client.Context(), wire, received);

    EXPECT_EQ(received.Size(), 32u);
    for (std::size_t i = 0; i < 20; ++i)
        EXPECT_TRUE(received.HasExact(client.Find(std::format("Shared.T{}", i))));
    for (std::size_t i = 0; i < 12; ++i)
        EXPECT_EQ(received.StackCount(client.Find(std::format("Owned.T{}", i))), i + 2);
}

TEST(GameplayTagContainerWire, AFewSharedTagsSendOnlyTheFirstRun)
{
    ReplicationLayout layout;
    ASSERT_TRUE(layout.AddCodec<GameplayTagContainer>());
    const ReplicatedComponent* component = layout.Find(ResolveComponentTypeId<GameplayTagContainer>());
    ASSERT_NE(component, nullptr);
    const TagMachine authority(GameplayTagContainer::Capacity, 0);

    const auto sentFor = [&](std::size_t shared) {
        GameplayTagContainerWire wire;
        ReplicationCodec<GameplayTagContainer>::ToWire(authority.Context(), Holding(authority, shared, 0), wire);
        return RunsSentToEveryone(*component, wire);
    };

    EXPECT_EQ(sentFor(0), 0u) << "an actor showing nothing sends no run";
    for (std::size_t shared = 1; shared <= GameplayTagContainer::Capacity; ++shared)
    {
        const std::size_t runs = (shared + kGameplayTagSharedRunLength - 1) / kGameplayTagSharedRunLength;
        const std::uint64_t sent = sentFor(shared);
        EXPECT_EQ(std::popcount(sent), static_cast<int>(runs)) << shared << " shared tags";
        EXPECT_EQ(ReplicationEncodedComponentBits(*component, sent),
                  component->Fields.size() + runs * kGameplayTagSharedRunLength * 32u)
            << shared << " shared tags cost the mask and their runs, nothing more";
    }
}
