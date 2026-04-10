#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>

#include <serialization/BinaryReader.h>
#include <serialization/BinaryWriter.h>

TEST(BinarySerializationTests, RoundTripsPrimitiveValues)
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);

    BinaryWriter writer(stream);

    const std::uint32_t expectedHealth = 150;
    const float expectedX = 42.5f;
    const float expectedY = -13.25f;

    ASSERT_TRUE(writer.Write(expectedHealth));
    ASSERT_TRUE(writer.Write(expectedX));
    ASSERT_TRUE(writer.Write(expectedY));

    stream.seekg(0);

    BinaryReader reader(stream);

    std::uint32_t actualHealth = 0;
    float actualX = 0.0f;
    float actualY = 0.0f;

    ASSERT_TRUE(reader.Read(actualHealth));
    ASSERT_TRUE(reader.Read(actualX));
    ASSERT_TRUE(reader.Read(actualY));

    EXPECT_EQ(actualHealth, expectedHealth);
    EXPECT_FLOAT_EQ(actualX, expectedX);
    EXPECT_FLOAT_EQ(actualY, expectedY);
}

struct PlayerRecord
{
    std::uint32_t Health;
    float X;
    float Y;
};

static bool Serialize_Player(BinaryWriter& writer, const PlayerRecord& record)
{
    return writer.Write(record.Health)
        && writer.Write(record.X)
        && writer.Write(record.Y);
}

static bool Deserialize_Player(BinaryReader& reader, PlayerRecord& record)
{
    return reader.Read(record.Health)
        && reader.Read(record.X)
        && reader.Read(record.Y);
}

TEST(BinarySerializationTests, RoundTripsPlayerRecord)
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);

    BinaryWriter writer(stream);

    const PlayerRecord expected{
        .Health = 99,
        .X = 10.5f,
        .Y = -2.25f
    };

    ASSERT_TRUE(Serialize_Player(writer, expected));

    stream.seekg(0);

    BinaryReader reader(stream);

    PlayerRecord actual{};
    ASSERT_TRUE(Deserialize_Player(reader, actual));

    EXPECT_EQ(actual.Health, expected.Health);
    EXPECT_FLOAT_EQ(actual.X, expected.X);
    EXPECT_FLOAT_EQ(actual.Y, expected.Y);
}

TEST(BinarySerializationTests, ReadFailsWhenStreamIsTruncated)
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);

    BinaryWriter writer(stream);

    const std::uint32_t expectedHealth = 123;
    ASSERT_TRUE(writer.Write(expectedHealth));

    stream.seekg(0);

    BinaryReader reader(stream);

    std::uint32_t health = 0;
    float x = 0.0f;

    ASSERT_TRUE(reader.Read(health));
    EXPECT_FALSE(reader.Read(x));
}