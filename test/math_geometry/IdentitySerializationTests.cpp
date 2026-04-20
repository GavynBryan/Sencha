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

    const AssetId expectedAsset{ 42 };
    const TypeId expectedType{ 7 };
    const SerializedEntityId expectedEntity{ 1001 };

    ASSERT_TRUE(Serialize(writer, expectedAsset));
    ASSERT_TRUE(Serialize(writer, expectedType));
    ASSERT_TRUE(Serialize(writer, expectedEntity));

    stream.seekg(0);
    BinaryReader reader(stream);

    AssetId actualAsset{};
    TypeId actualType{};
    SerializedEntityId actualEntity{};

    ASSERT_TRUE(Deserialize(reader, actualAsset));
    ASSERT_TRUE(Deserialize(reader, actualType));
    ASSERT_TRUE(Deserialize(reader, actualEntity));

    EXPECT_EQ(actualAsset, expectedAsset);
    EXPECT_EQ(actualType, expectedType);
    EXPECT_EQ(actualEntity, expectedEntity);
}

TEST(IdentitySerializationTests, DefaultIdIsFalsy)
{
    AssetId id{};
    EXPECT_FALSE(static_cast<bool>(id));
    EXPECT_TRUE(static_cast<bool>(AssetId{ 1 }));
}
