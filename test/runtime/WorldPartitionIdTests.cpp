#include <gtest/gtest.h>

#include <zone/WorldPartitionIds.h>

TEST(WorldPartitionId, ZoneIdHexRoundTrip)
{
    const ZoneId id{ 0x0123456789abcdefULL };
    const std::string text = ZoneIdToString(id);

    EXPECT_EQ(text.size(), 16u);
    EXPECT_EQ(text, "0123456789abcdef");

    const auto parsed = ZoneIdFromString(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, id);
}

TEST(WorldPartitionId, GraphIdHexRoundTrip)
{
    const GraphId id{ 0x00000000000000b1ULL };
    const auto parsed = GraphIdFromString(GraphIdToString(id));

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, id);
}

TEST(WorldPartitionId, TransitionIdHexRoundTrip)
{
    const TransitionId id{ 0xfedcba9876543210ULL };
    const auto parsed = TransitionIdFromString(TransitionIdToString(id));

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, id);
}

TEST(WorldPartitionId, IdFromStringRejectsMalformed)
{
    EXPECT_EQ(ZoneIdFromString("00000000000000a"), std::nullopt);
    EXPECT_EQ(ZoneIdFromString("00000000000000a12"), std::nullopt);
    EXPECT_EQ(ZoneIdFromString("00000000000000A1"), std::nullopt);
    EXPECT_EQ(ZoneIdFromString("00000000000000g1"), std::nullopt);
    EXPECT_EQ(ZoneIdFromString("0000000000000000"), std::nullopt);
    EXPECT_EQ(ZoneIdFromString(""), std::nullopt);
}
