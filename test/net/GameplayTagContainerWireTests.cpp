#include <gtest/gtest.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagContainerWire.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <net/ReplicationCodec.h>
#include <net/ReplicationImage.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationWireContext.h>

#include <algorithm>
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
            EXPECT_EQ(tags.Grant(machine.Find(std::format("Shared.T{}", i))), GameplayTagWrite::Added);
        for (std::size_t i = 0; i < owned; ++i)
            EXPECT_EQ(tags.Grant(machine.Find(std::format("Owned.T{}", i)), static_cast<std::uint16_t>(i + 2)),
                      GameplayTagWrite::Added);
        return tags;
    }

    struct TagLayout
    {
        ReplicationLayout Layout;
        const ReplicatedComponent* Component = nullptr;

        TagLayout()
        {
            EXPECT_TRUE(Layout.AddCodec<GameplayTagContainer>()) << Layout.ErrorDetail();
            Component = Layout.Find(ResolveComponentTypeId<GameplayTagContainer>());
        }
    };

    GameplayTagContainer RoundTrip(const ReplicatedComponent& component, const TagMachine& from, const TagMachine& to,
                                   const GameplayTagContainer& sent)
    {
        ReplicationImage image(component);
        ReplicationCodec<GameplayTagContainer>::ToWire(from.Context(), sent, image);
        GameplayTagContainer received;
        ReplicationCodec<GameplayTagContainer>::FromWire(to.Context(), image, received);
        return received;
    }
}

TEST(GameplayTagContainerWire, EveryTagAContainerCanHoldReachesEveryone)
{
    TagLayout tags;
    const std::size_t all = GameplayTagContainer::Capacity;
    const TagMachine authority(all, 0);
    const TagMachine client(all, 0, true);
    const GameplayTagContainer received = RoundTrip(*tags.Component, authority, client, Holding(authority, all, 0));
    ASSERT_EQ(received.Count, all);
    for (std::size_t i = 0; i < all; ++i)
        EXPECT_EQ(received.StackCount(client.Find(std::format("Shared.T{}", i))), 1u) << i;
}

TEST(GameplayTagContainerWire, OwnedAndSharedHalvesRoundTripTogether)
{
    TagLayout tags;
    const TagMachine authority(20, 12);
    const TagMachine client(20, 12, true);
    const GameplayTagContainer received = RoundTrip(*tags.Component, authority, client, Holding(authority, 20, 12));
    EXPECT_EQ(received.Count, 32u);
    for (std::size_t i = 0; i < 12; ++i)
        EXPECT_EQ(received.StackCount(client.Find(std::format("Owned.T{}", i))), i + 2) << i;
    for (std::size_t i = 0; i < 20; ++i)
        EXPECT_EQ(received.StackCount(client.Find(std::format("Shared.T{}", i))), 1u) << i;
}

TEST(GameplayTagContainerWire, TwoMachinesWriteOneImageForOneSet)
{
    TagLayout tags;
    const TagMachine authority(6, 6);
    const TagMachine client(6, 6, true);
    ASSERT_NE(authority.Find("Owned.T0"), client.Find("Owned.T0"));
    ReplicationImage sent(*tags.Component);
    ReplicationCodec<GameplayTagContainer>::ToWire(authority.Context(), Holding(authority, 6, 6), sent);
    ReplicationImage mirrored(*tags.Component);
    ReplicationCodec<GameplayTagContainer>::ToWire(client.Context(), Holding(client, 6, 6), mirrored);
    EXPECT_TRUE(std::ranges::equal(sent.Bytes(), mirrored.Bytes()))
        << "a receiver stages its own image under the authority's, position for position";
}

TEST(GameplayTagContainerWire, AFewSharedTagsCostOnlyThemselves)
{
    TagLayout tags;
    const ReplicatedComponent& component = *tags.Component;
    const TagMachine authority(GameplayTagContainer::Capacity, 0);
    const std::uint64_t sharedBit = std::uint64_t{ 1 } << (component.Fields.size() + 1);
    EXPECT_EQ(ReplicationVisibleFields(component, false), sharedBit) << "another peer sees the shared half alone";

    for (std::size_t shared = 0; shared <= GameplayTagContainer::Capacity; ++shared)
    {
        ReplicationImage image(component);
        ReplicationCodec<GameplayTagContainer>::ToWire(authority.Context(), Holding(authority, shared, 0), image);
        ASSERT_EQ(image.Count(1), shared);
        const std::vector<std::uint64_t> elements(shared, 1);
        EXPECT_EQ(ReplicationEncodedComponentBits(component, image.Bytes(), sharedBit, elements),
                  2u + NetVarUIntBits(shared) + shared * (1u + 32u))
            << shared << " shared tags cost the mask, the count and themselves, nothing more";
    }
}
