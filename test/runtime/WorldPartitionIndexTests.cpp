#include <gtest/gtest.h>

#include <zone/WorldPartitionIndex.h>

namespace
{

ZoneHeader MakeZone(uint64_t id)
{
    ZoneHeader zone;
    zone.Id = ZoneId{ id };
    zone.SceneRef = "levels/zone.level.json";
    zone.Bounds = Aabb3d{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } };
    return zone;
}

TransitionRecord MakeTransition(uint64_t id, uint64_t from, uint64_t to)
{
    TransitionRecord transition;
    transition.Id = TransitionId{ id };
    transition.From = ZoneId{ from };
    transition.To = ZoneId{ to };
    return transition;
}

} // namespace

TEST(WorldPartitionIndex, OutgoingAndIncomingAreSortedByIdValue)
{
    WorldPartitionManifest manifest;
    manifest.Zones = { MakeZone(0xa1), MakeZone(0xa2) };
    // Stored out of id order on purpose: the index must sort by id value.
    manifest.Transitions = {
        MakeTransition(0xc9, 0xa1, 0xa2),
        MakeTransition(0xc1, 0xa1, 0xa2),
        MakeTransition(0xc5, 0xa1, 0xa2),
    };

    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);

    const auto outgoing = index.Outgoing(ZoneId{ 0xa1 });
    ASSERT_EQ(outgoing.size(), 3u);
    EXPECT_EQ(manifest.Transitions[outgoing[0]].Id, TransitionId{ 0xc1 });
    EXPECT_EQ(manifest.Transitions[outgoing[1]].Id, TransitionId{ 0xc5 });
    EXPECT_EQ(manifest.Transitions[outgoing[2]].Id, TransitionId{ 0xc9 });

    const auto incoming = index.Incoming(ZoneId{ 0xa2 });
    ASSERT_EQ(incoming.size(), 3u);
    EXPECT_EQ(manifest.Transitions[incoming[0]].Id, TransitionId{ 0xc1 });
    EXPECT_EQ(manifest.Transitions[incoming[1]].Id, TransitionId{ 0xc5 });
    EXPECT_EQ(manifest.Transitions[incoming[2]].Id, TransitionId{ 0xc9 });

    EXPECT_TRUE(index.Outgoing(ZoneId{ 0xa2 }).empty());
    EXPECT_TRUE(index.Incoming(ZoneId{ 0xa1 }).empty());
}

TEST(WorldPartitionIndex, UnknownZoneYieldsEmptySpans)
{
    WorldPartitionManifest manifest;
    manifest.Zones = { MakeZone(0xa1) };

    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);

    EXPECT_FALSE(index.ContainsZone(ZoneId{ 0xff }));
    EXPECT_TRUE(index.Outgoing(ZoneId{ 0xff }).empty());
    EXPECT_TRUE(index.Incoming(ZoneId{ 0xff }).empty());
    EXPECT_TRUE(index.ContainsZone(ZoneId{ 0xa1 }));
}

TEST(WorldPartitionIndex, DanglingEdgesAreSkippedByBuild)
{
    WorldPartitionManifest manifest;
    manifest.Zones = { MakeZone(0xa1), MakeZone(0xa2) };
    manifest.Transitions = {
        MakeTransition(0xc1, 0xa1, 0xa2),
        MakeTransition(0xc2, 0xa1, 0xff),
        MakeTransition(0xc3, 0xff, 0xa2),
    };

    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);

    const auto outgoing = index.Outgoing(ZoneId{ 0xa1 });
    ASSERT_EQ(outgoing.size(), 1u);
    EXPECT_EQ(manifest.Transitions[outgoing[0]].Id, TransitionId{ 0xc1 });

    const auto incoming = index.Incoming(ZoneId{ 0xa2 });
    ASSERT_EQ(incoming.size(), 1u);
    EXPECT_EQ(manifest.Transitions[incoming[0]].Id, TransitionId{ 0xc1 });
}
