#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/BinaryFormat.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/Serialize.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <render/MeshRendererComponent.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneFormat.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>

#include <sstream>

namespace
{
    std::stringstream MakeBinaryStream()
    {
        return std::stringstream(std::ios::in | std::ios::out | std::ios::binary);
    }

    Registry MakeSceneRegistry()
    {
        Registry registry;
        auto& order = registry.Resources.Register<TransformPropagationOrderService>();
        registry.Resources.Register<TransformHierarchyService>();
        registry.Components.Register<TransformStore<Transform3f>>(order);
        registry.Components.Register<MeshRendererStore>();
        registry.Components.Register<CameraStore>();
        return registry;
    }

    void ResetSceneSerializers()
    {
        ClearComponentSerializers();
        InitSceneSerializer();
    }

    Transform3f MakeTransform(float x, float y, float z)
    {
        return Transform3f(
            Vec3d(x, y, z),
            Quatf::Identity(),
            Vec3d(1.0f, 2.0f, 3.0f));
    }
}

TEST(SceneSerializer, BinaryRoundTripsCleanRegistry)
{
    ResetSceneSerializers();
    Registry source = MakeSceneRegistry();

    EntityId parent = source.Entities.Create();
    EntityId child = source.Entities.Create();

    auto& transforms = source.Components.Get<TransformStore<Transform3f>>();
    auto& hierarchy = source.Resources.Get<TransformHierarchyService>();
    transforms.Add(parent, MakeTransform(1.0f, 2.0f, 3.0f));
    transforms.Add(child, MakeTransform(4.0f, 5.0f, 6.0f));
    hierarchy.Register(parent);
    hierarchy.SetParent(child, parent);

    MeshRendererComponent meshRenderer{
        .Mesh = MeshHandle{ .Index = 7, .Generation = 2 },
        .Material = MaterialHandle{ .Index = 9, .Generation = 3 },
        .Visible = false,
        .LayerMask = 0x00FF00FFu,
        .SubmeshMask = 0x0000000Fu,
    };
    source.Components.Get<MeshRendererStore>().Add(child, meshRenderer);

    CameraComponent camera{
        .Projection = ProjectionKind::Orthographic,
        .FovYRadians = 0.75f,
        .NearPlane = 0.25f,
        .FarPlane = 250.0f,
        .OrthographicHeight = 12.0f,
    };
    source.Components.Get<CameraStore>().Add(parent, camera);

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(SaveSceneBinary(source, writer));

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded;
    SceneLoadError error;
    ASSERT_TRUE(LoadSceneBinary(reader, loaded, &error)) << error.Message;

    EXPECT_EQ(loaded.Entities.Count(), 2u);

    auto& loadedTransforms = loaded.Components.Get<TransformStore<Transform3f>>();
    auto& loadedHierarchy = loaded.Resources.Get<TransformHierarchyService>();
    auto& loadedMeshes = loaded.Components.Get<MeshRendererStore>();
    auto& loadedCameras = loaded.Components.Get<CameraStore>();

    ASSERT_EQ(loadedTransforms.Count(), 2u);
    ASSERT_EQ(loadedMeshes.Count(), 1u);
    ASSERT_EQ(loadedCameras.Count(), 1u);

    const auto owners = loadedTransforms.GetOwnerIds();
    EntityId loadedParent{ owners[0], 1 };
    EntityId loadedChild{ owners[1], 1 };
    if (loadedHierarchy.GetParent(loadedParent).IsValid())
        std::swap(loadedParent, loadedChild);

    ASSERT_TRUE(loadedHierarchy.GetParent(loadedChild).IsValid());
    EXPECT_EQ(loadedHierarchy.GetParent(loadedChild).Index, loadedParent.Index);
    ASSERT_NE(loadedTransforms.TryGet(loadedParent), nullptr);
    EXPECT_EQ(loadedTransforms.TryGet(loadedParent)->Local.Position, Vec3d(1.0f, 2.0f, 3.0f));

    const MeshRendererComponent* loadedMesh = loadedMeshes.TryGet(loadedChild);
    ASSERT_NE(loadedMesh, nullptr);
    EXPECT_EQ(loadedMesh->Mesh, meshRenderer.Mesh);
    EXPECT_EQ(loadedMesh->Material, meshRenderer.Material);
    EXPECT_EQ(loadedMesh->Visible, meshRenderer.Visible);
    EXPECT_EQ(loadedMesh->LayerMask, meshRenderer.LayerMask);
    EXPECT_EQ(loadedMesh->SubmeshMask, meshRenderer.SubmeshMask);

    const CameraComponent* loadedCamera = loadedCameras.TryGet(loadedParent);
    ASSERT_NE(loadedCamera, nullptr);
    EXPECT_EQ(loadedCamera->Projection, ProjectionKind::Orthographic);
    EXPECT_FLOAT_EQ(loadedCamera->OrthographicHeight, 12.0f);
}

TEST(SceneSerializer, BinaryLoadIsAdditiveAndRemapsEntityIndices)
{
    ResetSceneSerializers();
    Registry source = MakeSceneRegistry();
    EntityId sourceEntity = source.Entities.Create();
    source.Resources.Get<TransformHierarchyService>().Register(sourceEntity);
    source.Components.Get<TransformStore<Transform3f>>().Add(sourceEntity, MakeTransform(8.0f, 0.0f, 0.0f));

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(SaveSceneBinary(source, writer));

    Registry loaded = MakeSceneRegistry();
    EntityId preexisting = loaded.Entities.Create();
    loaded.Components.Get<TransformStore<Transform3f>>().Add(preexisting, MakeTransform(-1.0f, 0.0f, 0.0f));

    stream.seekg(0);
    BinaryReader reader(stream);
    ASSERT_TRUE(LoadSceneBinary(reader, loaded));

    EXPECT_EQ(loaded.Entities.Count(), 2u);
    auto& transforms = loaded.Components.Get<TransformStore<Transform3f>>();
    ASSERT_EQ(transforms.Count(), 2u);
    EXPECT_NE(transforms.TryGet(EntityId{ sourceEntity.Index, sourceEntity.Generation })->Local.Position,
        Vec3d(8.0f, 0.0f, 0.0f));

    bool foundRemapped = false;
    for (const auto& component : transforms.GetItems())
        foundRemapped = foundRemapped || component.Local.Position == Vec3d(8.0f, 0.0f, 0.0f);
    EXPECT_TRUE(foundRemapped);
}

TEST(SceneSerializer, JsonRoundTripsThroughStringifyAndParser)
{
    ResetSceneSerializers();
    Registry source = MakeSceneRegistry();
    EntityId entity = source.Entities.Create();
    source.Resources.Get<TransformHierarchyService>().Register(entity);
    source.Components.Get<TransformStore<Transform3f>>().Add(entity, MakeTransform(2.0f, 3.0f, 4.0f));
    source.Components.Get<CameraStore>().Add(entity, CameraComponent{});

    JsonValue json = SaveSceneJson(source);
    std::string text = JsonStringify(json, true);
    auto parsed = JsonParse(text);
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded));

    ASSERT_EQ(loaded.Entities.Count(), 1u);
    auto& loadedTransforms = loaded.Components.Get<TransformStore<Transform3f>>();
    ASSERT_EQ(loadedTransforms.Count(), 1u);
    EXPECT_EQ(loadedTransforms.GetItems()[0].Local.Position, Vec3d(2.0f, 3.0f, 4.0f));
    EXPECT_EQ(loaded.Components.Get<CameraStore>().Count(), 1u);
}

TEST(SceneSerializer, LoadsHandAuthoredJson)
{
    ResetSceneSerializers();
    auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "Transform": {
                        "position": [0, 0, 0],
                        "rotation": [0, 0, 0, 1],
                        "scale": [1, 1, 1]
                    },
                    "MeshRenderer": {
                        "mesh": { "index": 3, "generation": 1 },
                        "material": { "index": 4, "generation": 2 },
                        "visible": true,
                        "layer_mask": 4294967295,
                        "submesh_mask": 15
                    }
                }
            },
            {
                "components": {
                    "Transform": {
                        "position": [5, 0, 0],
                        "rotation": [0, 0, 0, 1],
                        "scale": [1, 1, 1]
                    },
                    "Camera": {
                        "projection": "perspective",
                        "fov_y_radians": 1.0,
                        "near_plane": 0.1,
                        "far_plane": 1000.0
                    }
                }
            }
        ],
        "hierarchy": [
            { "child": 0, "parent": 1 }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded));

    EXPECT_EQ(loaded.Entities.Count(), 2u);
    EXPECT_EQ(loaded.Components.Get<TransformStore<Transform3f>>().Count(), 2u);
    EXPECT_EQ(loaded.Components.Get<MeshRendererStore>().Count(), 1u);
    EXPECT_EQ(loaded.Components.Get<CameraStore>().Count(), 1u);
    EXPECT_EQ(loaded.Resources.Get<TransformHierarchyService>().GetRoots().size(), 1u);
}

TEST(SceneSerializer, JsonLoadRollsBackEntitiesAndComponentsOnFailure)
{
    ResetSceneSerializers();
    auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "Transform": {
                        "position": [1, 2, 3],
                        "rotation": [0, 0, 0, 1],
                        "scale": [1, 1, 1]
                    }
                }
            }
        ],
        "hierarchy": [
            { "child": 0, "parent": 4 }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry loaded = MakeSceneRegistry();
    SceneLoadError error;
    EXPECT_FALSE(LoadSceneJson(*parsed, loaded, &error));

    EXPECT_EQ(loaded.Entities.Count(), 0u);
    EXPECT_EQ(loaded.Components.Get<TransformStore<Transform3f>>().Count(), 0u);
    EXPECT_EQ(loaded.Resources.Get<TransformHierarchyService>().Count(), 0u);
}

TEST(SceneSerializer, BinaryLoadRollsBackCreatedEntitiesOnFailure)
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
        ASSERT_TRUE(chunk.Begin(writer, SceneChunk::Cameras, SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 1 }));
        ASSERT_TRUE(Serialize(writer, EntityIndex{ 99 }));
        ASSERT_TRUE(chunk.End(writer));
    }

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded = MakeSceneRegistry();
    SceneLoadError error;
    EXPECT_FALSE(LoadSceneBinary(reader, loaded, &error));

    EXPECT_EQ(loaded.Entities.Count(), 0u);
    EXPECT_EQ(loaded.Components.Get<CameraStore>().Count(), 0u);
}

TEST(SceneSerializer, BinarySkipsUnknownChunks)
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
        ASSERT_TRUE(chunk.Begin(writer, MakeFourCC('T', 'E', 'S', 'T'), SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 0xDEADBEEFu }));
        ASSERT_TRUE(chunk.End(writer));
    }

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, SceneChunk::Cameras, SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 1 }));
        ASSERT_TRUE(Serialize(writer, EntityIndex{ 0 }));
        ASSERT_TRUE(Serialize(writer, CameraComponent{}));
        ASSERT_TRUE(chunk.End(writer));
    }

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded;
    ASSERT_TRUE(LoadSceneBinary(reader, loaded));

    EXPECT_EQ(loaded.Entities.Count(), 1u);
    EXPECT_EQ(loaded.Components.Get<CameraStore>().Count(), 1u);
}

TEST(SceneSerializer, HandlesEmptyRegistry)
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
    EXPECT_EQ(loaded.Entities.Count(), 0u);

    JsonValue json = SaveSceneJson(source);
    Registry jsonLoaded;
    ASSERT_TRUE(LoadSceneJson(json, jsonLoaded));
    EXPECT_EQ(jsonLoaded.Entities.Count(), 0u);
}
