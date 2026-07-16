#include <gtest/gtest.h>

#include <audio/AudioCaptionComponent.h>
#include <audio/AudioSourceComponent.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/BinaryFormat.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/Serialize.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <render/PointLightComponent.h>
#include <render/StaticMeshComponent.h>
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

    void InitializeSceneRegistry(Registry& registry)
    {
        registry.Entities.RegisterComponent<LocalTransform>();
        registry.Entities.RegisterComponent<WorldTransform>();
        registry.Entities.RegisterComponent<Parent>();
        registry.Entities.RegisterComponent<StaticMeshComponent>();
        registry.Entities.RegisterComponent<PointLightComponent>();
        registry.Entities.RegisterComponent<CameraComponent>();
        registry.Entities.RegisterComponent<AudioSourceComponent>();
        registry.Entities.RegisterComponent<AudioCaptionComponent>();
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

    void AddTransform(Registry& registry, EntityId entity, const Transform3f& transform)
    {
        registry.Entities.AddComponent(entity, LocalTransform{ transform });
        registry.Entities.AddComponent(entity, WorldTransform{ transform });
    }

    void SetParent(Registry& registry, EntityId child, EntityId parent)
    {
        registry.Entities.AddComponent(child, Parent{ parent });
    }
}

TEST(SceneSerializer, BinaryRoundTripsCleanRegistry)
{
    ResetSceneSerializers();
    Registry source;
    InitializeSceneRegistry(source);

    EntityId parent = source.Entities.CreateEntity();
    EntityId child = source.Entities.CreateEntity();

    AddTransform(source, parent, MakeTransform(1.0f, 2.0f, 3.0f));
    AddTransform(source, child, MakeTransform(4.0f, 5.0f, 6.0f));
    SetParent(source, child, parent);

    CameraComponent camera{
        .Projection = ProjectionKind::Orthographic,
        .FovYRadians = 0.75f,
        .NearPlane = 0.25f,
        .FarPlane = 250.0f,
        .OrthographicHeight = 12.0f,
    };
    source.Entities.AddComponent(parent, camera);

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(SaveSceneBinary(source, writer));

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded;
    SceneLoadError error;
    ASSERT_TRUE(LoadSceneBinary(reader, loaded, &error)) << error.Message;

    EXPECT_EQ(loaded.Entities.EntityCount(), 2u);

    ASSERT_EQ(loaded.Entities.CountComponents<LocalTransform>(), 2u);
    ASSERT_EQ(loaded.Entities.CountComponents<CameraComponent>(), 1u);
    ASSERT_EQ(loaded.Entities.CountComponents<Parent>(), 1u);

    EntityId loadedChild;
    EntityId loadedParent;
    loaded.Entities.ForEachComponent<Parent>(
        [&](EntityId childEntity, const Parent& parentComponent)
    {
        loadedChild = childEntity;
        loadedParent = parentComponent.Entity;
    });

    ASSERT_TRUE(loadedChild.IsValid());
    ASSERT_TRUE(loadedParent.IsValid());
    ASSERT_NE(loaded.Entities.TryGet<LocalTransform>(loadedParent), nullptr);
    EXPECT_EQ(
        loaded.Entities.TryGet<LocalTransform>(loadedParent)->Value.Position,
        Vec3d(1.0f, 2.0f, 3.0f));

    const CameraComponent* loadedCamera =
        loaded.Entities.TryGet<CameraComponent>(loadedParent);
    ASSERT_NE(loadedCamera, nullptr);
    EXPECT_EQ(loadedCamera->Projection, ProjectionKind::Orthographic);
    EXPECT_FLOAT_EQ(loadedCamera->OrthographicHeight, 12.0f);
}

TEST(SceneSerializer, BinaryLoadIsAdditiveAndRemapsEntityIndices)
{
    ResetSceneSerializers();
    Registry source;
    InitializeSceneRegistry(source);
    EntityId sourceEntity = source.Entities.CreateEntity();
    AddTransform(source, sourceEntity, MakeTransform(8.0f, 0.0f, 0.0f));

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(SaveSceneBinary(source, writer));

    Registry loaded;
    InitializeSceneRegistry(loaded);
    EntityId preexisting = loaded.Entities.CreateEntity();
    AddTransform(loaded, preexisting, MakeTransform(-1.0f, 0.0f, 0.0f));

    stream.seekg(0);
    BinaryReader reader(stream);
    ASSERT_TRUE(LoadSceneBinary(reader, loaded));

    EXPECT_EQ(loaded.Entities.EntityCount(), 2u);
    ASSERT_EQ(loaded.Entities.CountComponents<LocalTransform>(), 2u);
    if (const LocalTransform* staleSource = loaded.Entities.TryGet<LocalTransform>(
            EntityId{ sourceEntity.Index, sourceEntity.Generation }))
    {
        EXPECT_NE(staleSource->Value.Position, Vec3d(8.0f, 0.0f, 0.0f));
    }

    bool foundRemapped = false;
    loaded.Entities.ForEachComponent<LocalTransform>(
        [&](EntityId, const LocalTransform& component)
    {
        foundRemapped = foundRemapped
            || component.Value.Position == Vec3d(8.0f, 0.0f, 0.0f);
    });
    EXPECT_TRUE(foundRemapped);
}

TEST(SceneSerializer, JsonRoundTripsThroughStringifyAndParser)
{
    ResetSceneSerializers();
    Registry source;
    InitializeSceneRegistry(source);
    EntityId entity = source.Entities.CreateEntity();
    AddTransform(source, entity, MakeTransform(2.0f, 3.0f, 4.0f));
    source.Entities.AddComponent(entity, CameraComponent{});

    JsonValue json = SaveSceneJson(source);
    std::string text = JsonStringify(json, true);
    auto parsed = JsonParse(text);
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded));

    ASSERT_EQ(loaded.Entities.EntityCount(), 1u);
    ASSERT_EQ(loaded.Entities.CountComponents<LocalTransform>(), 1u);
    const LocalTransform* loadedTransform = nullptr;
    loaded.Entities.ForEachComponent<LocalTransform>(
        [&](EntityId, const LocalTransform& component)
    {
        loadedTransform = &component;
    });
    ASSERT_NE(loadedTransform, nullptr);
    EXPECT_EQ(loadedTransform->Value.Position, Vec3d(2.0f, 3.0f, 4.0f));
    EXPECT_EQ(loaded.Entities.CountComponents<CameraComponent>(), 1u);
}

TEST(SceneSerializer, PointLightRoundTripsThroughJson)
{
    ResetSceneSerializers();
    Registry source;
    InitializeSceneRegistry(source);
    EntityId entity = source.Entities.CreateEntity();
    AddTransform(source, entity, MakeTransform(0.0f, 0.0f, 0.0f));

    PointLightComponent light{};
    light.Color = Vec<3>(0.2f, 0.4f, 0.6f);
    light.Intensity = 2.5f;
    light.Range = 15.0f;
    light.Enabled = false;
    source.Entities.AddComponent(entity, light);

    JsonValue json = SaveSceneJson(source);
    auto parsed = JsonParse(JsonStringify(json, true));
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded));

    ASSERT_EQ(loaded.Entities.CountComponents<PointLightComponent>(), 1u);
    const PointLightComponent* out = nullptr;
    loaded.Entities.ForEachComponent<PointLightComponent>(
        [&](EntityId, const PointLightComponent& component)
    {
        out = &component;
    });
    ASSERT_NE(out, nullptr);
    EXPECT_FLOAT_EQ(out->Color.X, 0.2f);
    EXPECT_FLOAT_EQ(out->Color.Y, 0.4f);
    EXPECT_FLOAT_EQ(out->Color.Z, 0.6f);
    EXPECT_FLOAT_EQ(out->Intensity, 2.5f);
    EXPECT_FLOAT_EQ(out->Range, 15.0f);
    EXPECT_FALSE(out->Enabled);
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
                        "local": {
                            "position": [0, 0, 0],
                            "rotation": [0, 0, 0, 1],
                            "scale": [1, 1, 1]
                        }
                    },
                    "Camera": {
                        "projection": "orthographic",
                        "fov_y_radians": 1.0,
                        "orthographic_height": 8.0,
                        "near_plane": 0.1,
                        "far_plane": 100.0
                    }
                }
            },
            {
                "components": {
                    "Transform": {
                        "local": {
                            "position": [5, 0, 0],
                            "rotation": [0, 0, 0, 1],
                            "scale": [1, 1, 1]
                        }
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

    EXPECT_EQ(loaded.Entities.EntityCount(), 2u);
    EXPECT_EQ(loaded.Entities.CountComponents<LocalTransform>(), 2u);
    EXPECT_EQ(loaded.Entities.CountComponents<CameraComponent>(), 1u);
    EXPECT_EQ(loaded.Entities.CountComponents<Parent>(), 1u);
}

TEST(SceneSerializer, RegistersStaticMeshThroughGenericSerializer)
{
    ResetSceneSerializers();

    bool found = false;
    for (const auto& entry : GetComponentSerializerEntries())
        found = found || entry->JsonKey() == "StaticMesh";

    EXPECT_TRUE(found);
}
