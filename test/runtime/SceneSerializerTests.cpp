// Scene save/load round trips: what a registry writes must read back as the
// same entities, components, and hierarchy, across both the binary and JSON
// forms. Per-field asset codecs live in SceneFieldCodecTests.cpp; partial-load
// rollback lives in SceneSerializerFailureTests.cpp.

#include <gtest/gtest.h>
#include <world/transform/TransformComponents.h>

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <audio/AudioCaptionComponent.h>
#include <audio/AudioSourceComponent.h>
#include <controller/LookOrientation.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/BinaryFormat.h>
#include <core/serialization/Serialize.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <math/geometry/3d/Transform3d.h>
#include <physics/components/CharacterController.h>
#include <render/extract/Camera.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/registry/Registry.h>
#include <math/MathSchemas.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/serialization/SceneFormat.h>
#include <world/serialization/SceneSerializer.h>

#include <optional>
#include <sstream>

namespace
{

    Registry MakeSceneRegistry()
    {
        Registry registry;
        // The engine's own vocabulary, composed the way the runtime composes
        // it, so this fixture cannot know a different set of components than
        // the code it is testing.
        ComponentRegistrar components(registry.Components);
        RegisterEngineComponents(components);
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

//=============================================================================
// The components a controlled body is made of
//
// Their authored forms are what let content describe a body instead of code
// building one. Two of them persist as names, because the runtime values are
// registration-order ids; the rest are ordinary schema components whose only
// missing piece was a chunk id.
//=============================================================================

TEST(SceneSerializer, AControlledBodyRoundTripsThroughJson)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    Registry source = MakeSceneRegistry();
    EntityId entity = source.Components.CreateEntity();
    AddTransform(source, entity, MakeTransform(0.0f, 0.0f, 0.0f));

    CharacterController controller{};
    controller.Radius = 0.42f;
    controller.StepHeight = 0.5f;
    source.Components.AddComponent(entity, controller);

    LookOrientation look{};
    look.Yaw = 1.25f;
    look.MaxPitch = 0.9f;
    source.Components.AddComponent(entity, look);

    source.Components.AddComponent(entity, AimFacing{});

    JsonValue json = SaveSceneJson(source, serializers);
    auto parsed = JsonParse(JsonStringify(json, true));
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers));

    ASSERT_EQ(loaded.Components.CountComponents<CharacterController>(), 1u);
    loaded.Components.ForEachComponent<CharacterController>(
        [](EntityId, const CharacterController& c)
    {
        EXPECT_FLOAT_EQ(c.Radius, 0.42f);
        EXPECT_FLOAT_EQ(c.StepHeight, 0.5f);
        // Untouched dimensions come back as the initializers, not as zero.
        EXPECT_FLOAT_EQ(c.Height, CharacterController{}.Height);
    });

    ASSERT_EQ(loaded.Components.CountComponents<LookOrientation>(), 1u);
    loaded.Components.ForEachComponent<LookOrientation>(
        [](EntityId, const LookOrientation& c)
    {
        EXPECT_FLOAT_EQ(c.Yaw, 1.25f);
        EXPECT_FLOAT_EQ(c.MaxPitch, 0.9f);
    });

    // A tag's presence is its whole value.
    EXPECT_EQ(loaded.Components.CountComponents<AimFacing>(), 1u);
}

// A component whose fields are all absent still arrives, at its initializers.
// This is what lets a prefab opt a body into a capsule without stating one.
TEST(SceneSerializer, AnEmptyControllerLoadsAtItsInitializers)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "CharacterController": {},
                    "AimFacing": {}
                }
            }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers));

    ASSERT_EQ(loaded.Components.CountComponents<CharacterController>(), 1u);
    loaded.Components.ForEachComponent<CharacterController>(
        [](EntityId, const CharacterController& c)
    {
        const CharacterController defaults{};
        EXPECT_FLOAT_EQ(c.Radius, defaults.Radius);
        EXPECT_FLOAT_EQ(c.Height, defaults.Height);
        EXPECT_FLOAT_EQ(c.SlopeLimitDegrees, defaults.SlopeLimitDegrees);
        EXPECT_FLOAT_EQ(c.SkinWidth, defaults.SkinWidth);
    });
    EXPECT_EQ(loaded.Components.CountComponents<AimFacing>(), 1u);
}

// Tags and attributes save the registered NAME, so a destination that
// registered the same vocabulary in a different order still reads the same
// values -- and reads them at all, which is what registering these serializers
// beside their components bought.
TEST(SceneSerializer, TagsAndAttributesRoundTripByName)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();

    Registry source = MakeSceneRegistry();
    auto& sourceTags = source.Components.AddResource<GameplayTagRegistry>();
    auto& sourceAttributes = source.Components.AddResource<AttributeRegistry>();
    const std::optional<GameplayTagId> controlled =
        sourceTags.RegisterTag("movement.controlled");
    ASSERT_TRUE(controlled.has_value());
    const AttributeId speed =
        sourceAttributes.RegisterAttribute("MoveSpeed", 0.0f, 100.0f, 6.0f);

    EntityId entity = source.Components.CreateEntity();
    AddTransform(source, entity, MakeTransform(0.0f, 0.0f, 0.0f));

    GameplayTagContainer tags{};
    ASSERT_TRUE(tags.Grant(*controlled));
    source.Components.AddComponent(entity, tags);

    AttributeSet attributes{};
    ASSERT_TRUE(attributes.Add(speed, 4.5f));
    source.Components.AddComponent(entity, attributes);

    JsonValue json = SaveSceneJson(source, serializers);
    auto parsed = JsonParse(JsonStringify(json, true));
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    auto& loadedTags = loaded.Components.AddResource<GameplayTagRegistry>();
    auto& loadedAttributes = loaded.Components.AddResource<AttributeRegistry>();
    // Deliberately a different registration order, so a match on raw ids would
    // be a coincidence rather than a round trip.
    (void)loadedTags.RegisterTag("decoy.first");
    (void)loadedAttributes.RegisterAttribute("Decoy", 0.0f, 1.0f, 0.0f);
    const std::optional<GameplayTagId> loadedControlled =
        loadedTags.RegisterTag("movement.controlled");
    ASSERT_TRUE(loadedControlled.has_value());
    const AttributeId loadedSpeed =
        loadedAttributes.RegisterAttribute("MoveSpeed", 0.0f, 100.0f, 6.0f);
    ASSERT_NE(loadedControlled->Value, controlled->Value);
    ASSERT_NE(loadedSpeed.Value, speed.Value);

    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers));

    ASSERT_EQ(loaded.Components.CountComponents<GameplayTagContainer>(), 1u);
    loaded.Components.ForEachComponent<GameplayTagContainer>(
        [&](EntityId, const GameplayTagContainer& c)
    {
        EXPECT_TRUE(c.HasExact(*loadedControlled));
    });

    ASSERT_EQ(loaded.Components.CountComponents<AttributeSet>(), 1u);
    loaded.Components.ForEachComponent<AttributeSet>(
        [&](EntityId, const AttributeSet& c)
    {
        EXPECT_FLOAT_EQ(c.GetBase(loadedSpeed), 4.5f);
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
