// Scene save/load round trips: what a registry writes must read back as the
// same entities, components, and hierarchy, across both the binary and JSON
// forms. Per-field asset codecs live in SceneFieldCodecTests.cpp; partial-load
// rollback lives in SceneSerializerFailureTests.cpp.

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
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/registry/Registry.h>
#include <world/ComponentManifest.h>
#include <world/serialization/SceneFormat.h>
#include <world/serialization/SceneSerializer.h>

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
        ForEachSceneComponent([&]<typename T>(ComponentTag<T>)
        {
            registry.Components.RegisterComponent<T>();
        });
        registry.Components.RegisterComponent<WorldTransform>();
        registry.Components.RegisterComponent<Parent>();
        return registry;
    }

    ComponentSerializerRegistry MakeSerializers()
    {
        ComponentSerializerRegistry serializers;
        RegisterEngineSceneSerializers(serializers);
        return serializers;
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
        registry.Components.AddComponent(entity, LocalTransform{ transform });
        registry.Components.AddComponent(entity, WorldTransform{ transform });
    }

    void SetParent(Registry& registry, EntityId child, EntityId parent)
    {
        registry.Components.AddComponent(child, Parent{ parent });
    }
}
TEST(SceneSerializer, BinaryRoundTripsCleanRegistry)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    Registry source = MakeSceneRegistry();

    EntityId parent = source.Components.CreateEntity();
    EntityId child = source.Components.CreateEntity();

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
    source.Components.AddComponent(parent, camera);

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(SaveSceneBinary(source, serializers, writer));

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded;
    SceneLoadError error;
    ASSERT_TRUE(LoadSceneBinary(reader, loaded, serializers, &error)) << error.Message;

    EXPECT_EQ(loaded.Components.EntityCount(), 2u);

    ASSERT_EQ(loaded.Components.CountComponents<LocalTransform>(), 2u);
    ASSERT_EQ(loaded.Components.CountComponents<CameraComponent>(), 1u);
    ASSERT_EQ(loaded.Components.CountComponents<Parent>(), 1u);

    EntityId loadedChild;
    EntityId loadedParent;
    loaded.Components.ForEachComponent<Parent>([&](EntityId childEntity, const Parent& parentComponent)
    {
        loadedChild = childEntity;
        loadedParent = parentComponent.Entity;
    });

    ASSERT_TRUE(loadedChild.IsValid());
    ASSERT_TRUE(loadedParent.IsValid());
    ASSERT_NE(loaded.Components.TryGet<LocalTransform>(loadedParent), nullptr);
    EXPECT_EQ(loaded.Components.TryGet<LocalTransform>(loadedParent)->Value.Position, Vec3d(1.0f, 2.0f, 3.0f));

    const CameraComponent* loadedCamera = loaded.Components.TryGet<CameraComponent>(loadedParent);
    ASSERT_NE(loadedCamera, nullptr);
    EXPECT_EQ(loadedCamera->Projection, ProjectionKind::Orthographic);
    EXPECT_FLOAT_EQ(loadedCamera->OrthographicHeight, 12.0f);
}

TEST(SceneSerializer, BinaryLoadIsAdditiveAndRemapsEntityIndices)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    Registry source = MakeSceneRegistry();
    EntityId sourceEntity = source.Components.CreateEntity();
    AddTransform(source, sourceEntity, MakeTransform(8.0f, 0.0f, 0.0f));

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(SaveSceneBinary(source, serializers, writer));

    Registry loaded = MakeSceneRegistry();
    EntityId preexisting = loaded.Components.CreateEntity();
    AddTransform(loaded, preexisting, MakeTransform(-1.0f, 0.0f, 0.0f));

    stream.seekg(0);
    BinaryReader reader(stream);
    ASSERT_TRUE(LoadSceneBinary(reader, loaded, serializers));

    EXPECT_EQ(loaded.Components.EntityCount(), 2u);
    ASSERT_EQ(loaded.Components.CountComponents<LocalTransform>(), 2u);
    if (const LocalTransform* staleSource = loaded.Components.TryGet<LocalTransform>(
            EntityId{ sourceEntity.Index, sourceEntity.Generation }))
    {
        EXPECT_NE(staleSource->Value.Position, Vec3d(8.0f, 0.0f, 0.0f));
    }

    bool foundRemapped = false;
    loaded.Components.ForEachComponent<LocalTransform>([&](EntityId, const LocalTransform& component)
    {
        foundRemapped = foundRemapped || component.Value.Position == Vec3d(8.0f, 0.0f, 0.0f);
    });
    EXPECT_TRUE(foundRemapped);
}

TEST(SceneSerializer, JsonRoundTripsThroughStringifyAndParser)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    Registry source = MakeSceneRegistry();
    EntityId entity = source.Components.CreateEntity();
    AddTransform(source, entity, MakeTransform(2.0f, 3.0f, 4.0f));
    source.Components.AddComponent(entity, CameraComponent{});

    JsonValue json = SaveSceneJson(source, serializers);
    std::string text = JsonStringify(json, true);
    auto parsed = JsonParse(text);
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers));

    ASSERT_EQ(loaded.Components.EntityCount(), 1u);
    ASSERT_EQ(loaded.Components.CountComponents<LocalTransform>(), 1u);
    const LocalTransform* loadedTransform = nullptr;
    loaded.Components.ForEachComponent<LocalTransform>([&](EntityId, const LocalTransform& component)
    {
        loadedTransform = &component;
    });
    ASSERT_NE(loadedTransform, nullptr);
    EXPECT_EQ(loadedTransform->Value.Position, Vec3d(2.0f, 3.0f, 4.0f));
    EXPECT_EQ(loaded.Components.CountComponents<CameraComponent>(), 1u);
}

TEST(SceneSerializer, PointLightRoundTripsThroughJson)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    Registry source = MakeSceneRegistry();
    EntityId entity = source.Components.CreateEntity();
    AddTransform(source, entity, MakeTransform(0.0f, 0.0f, 0.0f));

    PointLightComponent light{};
    light.Color = Vec<3>(0.2f, 0.4f, 0.6f);
    light.Intensity = 2.5f;
    light.Range = 15.0f;
    light.Enabled = false;
    source.Components.AddComponent(entity, light);

    JsonValue json = SaveSceneJson(source, serializers);
    auto parsed = JsonParse(JsonStringify(json, true));
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers));

    ASSERT_EQ(loaded.Components.CountComponents<PointLightComponent>(), 1u);
    const PointLightComponent* out = nullptr;
    loaded.Components.ForEachComponent<PointLightComponent>([&](EntityId, const PointLightComponent& c)
    {
        out = &c;
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
    const ComponentSerializerRegistry serializers = MakeSerializers();
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
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers));

    EXPECT_EQ(loaded.Components.EntityCount(), 2u);
    EXPECT_EQ(loaded.Components.CountComponents<LocalTransform>(), 2u);
    EXPECT_EQ(loaded.Components.CountComponents<CameraComponent>(), 1u);
    EXPECT_EQ(loaded.Components.CountComponents<Parent>(), 1u);
}

TEST(SceneSerializer, LoadsLightRecordsCookedBeforeShadowFieldsExisted)
{
    // Scenes cooked before the shadow and bake fields existed carry only the
    // original light keys; the schema defaults must fill the rest instead of
    // rejecting the component.
    const ComponentSerializerRegistry serializers = MakeSerializers();
    auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "Transform": {
                        "local": {
                            "position": [0, 4, 8],
                            "rotation": [0, 0, 0, 1],
                            "scale": [1, 1, 1]
                        }
                    },
                    "PointLight": {
                        "color": [1, 0.7, 0.3],
                        "intensity": 8,
                        "range": 10,
                        "enabled": true
                    }
                }
            },
            {
                "components": {
                    "Transform": {
                        "local": {
                            "position": [0, 6, 8],
                            "rotation": [0, 0, 0, 1],
                            "scale": [1, 1, 1]
                        }
                    },
                    "SpotLight": {
                        "color": [1, 1, 1],
                        "intensity": 2,
                        "range": 12,
                        "inner_angle_degrees": 20,
                        "outer_angle_degrees": 35,
                        "enabled": true
                    }
                }
            }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers));

    ASSERT_EQ(loaded.Components.CountComponents<PointLightComponent>(), 1u);
    ASSERT_EQ(loaded.Components.CountComponents<SpotLightComponent>(), 1u);
    loaded.Components.ForEachComponent<PointLightComponent>(
        [](EntityId, const PointLightComponent& light)
    {
        EXPECT_FLOAT_EQ(light.Intensity, 8.0f);
        EXPECT_FALSE(light.CastShadows);
        EXPECT_EQ(light.ShadowResolution, ShadowResolutionTier::Medium);
        EXPECT_EQ(light.ShadowUpdate, ShadowUpdatePolicy::OnChange);
        EXPECT_EQ(light.BakeContribution, LightBakeContribution::None);
    });
    loaded.Components.ForEachComponent<SpotLightComponent>(
        [](EntityId, const SpotLightComponent& light)
    {
        EXPECT_FLOAT_EQ(light.Range, 12.0f);
        EXPECT_FALSE(light.CastShadows);
        EXPECT_EQ(light.ShadowUpdate, ShadowUpdatePolicy::OnChange);
        EXPECT_FLOAT_EQ(light.ShadowSoftness, 1.5f);
        EXPECT_EQ(light.BakeContribution, LightBakeContribution::None);
    });
}

TEST(SceneSerializer, RegistersStaticMeshThroughGenericSerializer)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();

    bool found = false;
    for (const auto& entry : serializers.Entries())
        found = found || entry->JsonKey() == "StaticMesh";

    EXPECT_TRUE(found);
}
