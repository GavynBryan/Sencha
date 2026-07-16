#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <core/serialization/BinaryFormat.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/Serialize.h>
#include <render/Camera.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneFormat.h>
#include <world/serialization/SceneSerializer.h>

#include <sstream>

namespace
{
    std::stringstream MakeBinaryStream()
    {
        return std::stringstream(std::ios::in | std::ios::out | std::ios::binary);
    }

    void InitializeFailureRegistry(Registry& registry)
    {
        registry.Components.RegisterComponent<LocalTransform>();
        registry.Components.RegisterComponent<WorldTransform>();
        registry.Components.RegisterComponent<Parent>();
        registry.Components.RegisterComponent<CameraComponent>();
    }

    void ResetSceneSerializers()
    {
        ClearComponentSerializers();
        InitSceneSerializer();
    }
}

TEST(SceneSerializerFailure, JsonLoadRollsBackEntitiesAndComponents)
{
    ResetSceneSerializers();
    auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "Transform": {
                        "local": {
                            "position": [1, 2, 3],
                            "rotation": [0, 0, 0, 1],
                            "scale": [1, 1, 1]
                        }
                    }
                }
            }
        ],
        "hierarchy": [
            { "child": 0, "parent": 4 }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    InitializeFailureRegistry(loaded);
    SceneLoadError error;
    EXPECT_FALSE(LoadSceneJson(*parsed, loaded, &error));

    EXPECT_EQ(loaded.Components.EntityCount(), 0u);
    EXPECT_EQ(loaded.Components.CountComponents<LocalTransform>(), 0u);
}

TEST(SceneSerializerFailure, BinaryLoadRollsBackCreatedEntities)
{
    ResetSceneSerializers();
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    ASSERT_TRUE(WriteBinaryHeader(writer, SceneMagic, SceneVersion));

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, SceneChunk::Registry, SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 1 }));
        ASSERT_TRUE(Serialize(writer, EntityIndex{ 0 }));
        ASSERT_TRUE(Serialize(writer, std::uint16_t{ 1 }));
        ASSERT_TRUE(chunk.End(writer));
    }

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(
            writer,
            TypeSchema<CameraComponent>::SceneChunkId,
            SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 1 }));
        ASSERT_TRUE(Serialize(writer, EntityIndex{ 99 }));
        ASSERT_TRUE(chunk.End(writer));
    }

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded;
    InitializeFailureRegistry(loaded);
    SceneLoadError error;
    EXPECT_FALSE(LoadSceneBinary(reader, loaded, &error));

    EXPECT_EQ(loaded.Components.EntityCount(), 0u);
    EXPECT_EQ(loaded.Components.CountComponents<CameraComponent>(), 0u);
}

TEST(SceneSerializerFailure, BinarySkipsUnknownChunks)
{
    ResetSceneSerializers();
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    ASSERT_TRUE(WriteBinaryHeader(writer, SceneMagic, SceneVersion));

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, SceneChunk::Registry, SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 1 }));
        ASSERT_TRUE(Serialize(writer, EntityIndex{ 0 }));
        ASSERT_TRUE(Serialize(writer, std::uint16_t{ 1 }));
        ASSERT_TRUE(chunk.End(writer));
    }

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(
            writer,
            MakeFourCC('T', 'E', 'S', 'T'),
            SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 0xDEADBEEFu }));
        ASSERT_TRUE(chunk.End(writer));
    }

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(
            writer,
            TypeSchema<CameraComponent>::SceneChunkId,
            SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 1 }));
        ASSERT_TRUE(Serialize(writer, EntityIndex{ 0 }));
        ASSERT_TRUE(Serialize(writer, CameraComponent{}));
        ASSERT_TRUE(chunk.End(writer));
    }

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded;
    ASSERT_TRUE(LoadSceneBinary(reader, loaded));

    EXPECT_EQ(loaded.Components.EntityCount(), 1u);
    EXPECT_EQ(loaded.Components.CountComponents<CameraComponent>(), 1u);
}

TEST(SceneSerializerFailure, HandlesEmptyRegistry)
{
    ResetSceneSerializers();
    Registry source;
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(SaveSceneBinary(source, writer));

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded;
    ASSERT_TRUE(LoadSceneBinary(reader, loaded));
    EXPECT_EQ(loaded.Components.EntityCount(), 0u);

    JsonValue json = SaveSceneJson(source);
    Registry jsonLoaded;
    ASSERT_TRUE(LoadSceneJson(json, jsonLoaded));
    EXPECT_EQ(jsonLoaded.Components.EntityCount(), 0u);
}
