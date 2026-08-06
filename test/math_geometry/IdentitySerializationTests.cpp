#include <gtest/gtest.h>

#include <core/identity/Id.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>

#include <sstream>

static std::stringstream MakeIdentityBinaryStream()
{
    return std::stringstream(std::ios::in | std::ios::out | std::ios::binary);
}

TEST(IdentitySerializationTests, RoundTripsIdTypes)
{
    auto stream = MakeIdentityBinaryStream();
    BinaryWriter writer(stream);

    const TypeId expectedType{ 7 };
    const PersistentEntityId expectedEntity{ 0x0123456789abcdefull };

    ASSERT_TRUE(Serialize(writer, expectedType));
    ASSERT_TRUE(Serialize(writer, expectedEntity));

    stream.seekg(0);
    BinaryReader reader(stream);

    TypeId actualType{};
    PersistentEntityId actualEntity{};

    ASSERT_TRUE(Deserialize(reader, actualType));
    ASSERT_TRUE(Deserialize(reader, actualEntity));

    EXPECT_EQ(actualType, expectedType);
    EXPECT_EQ(actualEntity, expectedEntity);
}

TEST(IdentitySerializationTests, DefaultIdIsFalsy)
{
    TypeId id{};
    EXPECT_FALSE(static_cast<bool>(id));
    EXPECT_TRUE(static_cast<bool>(TypeId{ 1 }));
}

TEST(IdentitySerializationTests, PersistentEntityIdTextRoundTrips)
{
    const PersistentEntityId id{ 0x0123456789abcdefull };
    const std::string text = PersistentEntityIdToString(id);
    EXPECT_EQ(text, "0123456789abcdef");

    const auto parsed = PersistentEntityIdFromString(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, id);
}

TEST(IdentitySerializationTests, PersistentEntityIdTextParsingIsStrict)
{
    EXPECT_FALSE(PersistentEntityIdFromString("").has_value());
    EXPECT_FALSE(PersistentEntityIdFromString("123").has_value());
    EXPECT_FALSE(PersistentEntityIdFromString("0000000000000000").has_value());
    EXPECT_FALSE(PersistentEntityIdFromString("0123456789ABCDEF").has_value());
    EXPECT_FALSE(PersistentEntityIdFromString("0123456789abcdef0").has_value());
    EXPECT_FALSE(PersistentEntityIdFromString("not-a-hex-number").has_value());
}
