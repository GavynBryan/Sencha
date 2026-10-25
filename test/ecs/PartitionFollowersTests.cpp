#include <ecs/PartitionFollowers.h>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace
{
    // Each dependent goes wherever the entity it is linked to goes.
    struct Links
    {
        std::vector<std::pair<EntityId, EntityId>> Pairs;
        std::size_t Batches = 0;

        static void Follow(void* context, World& world, std::span<const EntityPartitionMove> moves)
        {
            Links& links = *static_cast<Links*>(context);
            ++links.Batches;
            for (const EntityPartitionMove& move : moves)
                for (const auto& [leader, dependent] : links.Pairs)
                    if (leader == move.Entity && world.IsAlive(dependent))
                        (void)world.MoveEntityToPartition(dependent, move.Current);
        }
    };

    constexpr StoragePartitionId kZone{ 3 };
    constexpr StoragePartitionId kElsewhere{ 4 };
}

TEST(PartitionFollowers, DispatchingDrainsTheJournalWithNoFollowers)
{
    World world;
    for (int i = 0; i < 3; ++i)
        (void)world.MoveEntityToPartition(world.CreateEntity(kZone), kElsewhere);
    EXPECT_EQ(DispatchPartitionMoves(world), 3u);
    EXPECT_TRUE(world.PendingPartitionMoves().empty());
    EXPECT_EQ(DispatchPartitionMoves(world), 0u);
}

TEST(PartitionFollowers, AChainFollowsItsHeadOneLevelPerRound)
{
    World world;
    const EntityId head = world.CreateEntity(kZone);
    const EntityId middle = world.CreateEntity(kZone);
    const EntityId tail = world.CreateEntity(kZone);
    Links links{ .Pairs = { { head, middle }, { middle, tail } } };
    const PartitionFollowerId id = world.AddResource<PartitionFollowers>().Register(&links, &Links::Follow);
    ASSERT_TRUE(id.IsValid());

    ASSERT_TRUE(world.MoveEntityToPartition(head, kElsewhere));
    EXPECT_EQ(DispatchPartitionMoves(world), 3u);
    EXPECT_EQ(world.GetEntityPartition(middle), kElsewhere);
    EXPECT_EQ(world.GetEntityPartition(tail), kElsewhere);
    EXPECT_EQ(links.Batches, 3u) << "the head's move, then the middle's, then the tail's";
    EXPECT_TRUE(world.PendingPartitionMoves().empty());
}

TEST(PartitionFollowers, AnUnregisteredFollowerSeesNothing)
{
    World world;
    const EntityId head = world.CreateEntity(kZone);
    const EntityId dependent = world.CreateEntity(kZone);
    Links links{ .Pairs = { { head, dependent } } };
    PartitionFollowers& followers = world.AddResource<PartitionFollowers>();
    followers.Unregister(followers.Register(&links, &Links::Follow));
    EXPECT_EQ(followers.Count(), 0u);

    ASSERT_TRUE(world.MoveEntityToPartition(head, kElsewhere));
    (void)DispatchPartitionMoves(world);
    EXPECT_EQ(links.Batches, 0u);
    EXPECT_EQ(world.GetEntityPartition(dependent), kZone);
}
