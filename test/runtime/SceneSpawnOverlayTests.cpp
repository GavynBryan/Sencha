#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/World.h>
#include <runtime/spawn/SceneSpawnOverlay.h>
#include <world/build/EntityBuildPackage.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

#include <string>
#include <tuple>

struct OverlayVitals
{
    float Current = 100.0f;
    float Max = 100.0f;
};

template <>
struct TypeSchema<OverlayVitals>
{
    static constexpr std::string_view Name = "overlay_vitals";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('O', 'V', 'V', 'T');
    static auto Fields()
    {
        return std::tuple{ MakeField("current", &OverlayVitals::Current), MakeField("max", &OverlayVitals::Max) };
    }
};

struct OverlayBadge
{
    int Value = 0;
};

template <>
struct TypeSchema<OverlayBadge>
{
    static constexpr std::string_view Name = "overlay_badge";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('O', 'V', 'B', 'G');
    static auto Fields() { return std::tuple{ MakeField("value", &OverlayBadge::Value) }; }
};

namespace
{
    class SceneSpawnOverlayTest : public ::testing::Test
    {
    protected:
        SceneSpawnOverlayTest()
            : Context(Logging)
        {
            Entities.RegisterComponent<OverlayVitals>();
            Entities.RegisterComponent<OverlayBadge>();
            (void)Serializers.Register(std::make_unique<ComponentSerializer<OverlayVitals>>());
            (void)Serializers.Register(std::make_unique<ComponentSerializer<OverlayBadge>>());
        }

        static EntityBuildPackage Prefab(int roots = 1)
        {
            EntityBuildPackage package;
            for (int i = 0; i < roots; ++i)
            {
                const PackageEntityId root = package.CreateEntity();
                (void)package.SetSerializedComponent(
                    root, ResolveComponentTypeId<OverlayVitals>(),
                    JsonValue(JsonValue::Object{ { "current", JsonValue(100.0) }, { "max", JsonValue(100.0) } }));
            }
            return package;
        }

        static std::string RootComponent(const EntityBuildPackage& package, ComponentTypeId type)
        {
            const PackageComponent* component = package.FindComponent(PackageEntityId{ 0 }, type);
            return component != nullptr && component->SerializedJson ? JsonStringify(*component->SerializedJson) : "";
        }

        LoggingProvider Logging;
        SceneSerializationContext Context;
        World Entities;
        ComponentSerializerRegistry Serializers;
    };
}

TEST_F(SceneSpawnOverlayTest, AnEntityAsItsPrefabLeavesNothingToKeep)
{
    const EntityId body = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(body, OverlayVitals{});
    const SceneSpawnOverlay overlay = CaptureSpawnOverlay(Entities, body, Prefab(), Serializers, Context);
    EXPECT_TRUE(overlay.Empty()) << JsonStringify(overlay.RootPatch);
}

TEST_F(SceneSpawnOverlayTest, WhatChangedAndWhatWasAddedComeBackOverTheSamePrefab)
{
    const EntityId body = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(body, OverlayVitals{ .Current = 50.0f, .Max = 100.0f });
    Entities.AddComponent<OverlayBadge>(body, OverlayBadge{ .Value = 7 });
    const SceneSpawnOverlay overlay = CaptureSpawnOverlay(Entities, body, Prefab(), Serializers, Context);
    EXPECT_EQ(JsonStringify(overlay.RootPatch), R"({"overlay_vitals":{"current":50},"overlay_badge":{"value":7}})");

    EntityBuildPackage restored = Prefab();
    std::string error;
    ASSERT_TRUE(ApplySpawnOverlay(restored, overlay, Serializers, error)) << error;
    EXPECT_EQ(RootComponent(restored, ResolveComponentTypeId<OverlayVitals>()), R"({"current":50,"max":100})");
    EXPECT_EQ(RootComponent(restored, ResolveComponentTypeId<OverlayBadge>()), R"({"value":7})");
}

TEST_F(SceneSpawnOverlayTest, ARetunedPrefabStillAppliesWhatTheSaveDidNotChange)
{
    SceneSpawnOverlay overlay;
    overlay.RootPatch = JsonValue(JsonValue::Object{
        { "overlay_vitals", JsonValue(JsonValue::Object{ { "current", JsonValue(50.0) } }) } });
    EntityBuildPackage retuned;
    const PackageEntityId root = retuned.CreateEntity();
    (void)retuned.SetSerializedComponent(
        root, ResolveComponentTypeId<OverlayVitals>(),
        JsonValue(JsonValue::Object{ { "current", JsonValue(150.0) }, { "max", JsonValue(150.0) } }));
    std::string error;
    ASSERT_TRUE(ApplySpawnOverlay(retuned, overlay, Serializers, error)) << error;
    EXPECT_EQ(RootComponent(retuned, ResolveComponentTypeId<OverlayVitals>()), R"({"current":50,"max":150})");
}

TEST_F(SceneSpawnOverlayTest, APatchIsRefusedForASceneWithoutOneRootOrAComponentThisBuildLacks)
{
    SceneSpawnOverlay overlay;
    overlay.RootPatch = JsonValue(JsonValue::Object{
        { "overlay_vitals", JsonValue(JsonValue::Object{ { "current", JsonValue(1.0) } }) } });
    EntityBuildPackage twoRoots = Prefab(2);
    std::string error;
    EXPECT_FALSE(ApplySpawnOverlay(twoRoots, overlay, Serializers, error));

    overlay.RootPatch = JsonValue(JsonValue::Object{ { "not_here", JsonValue(JsonValue::Object{}) } });
    EntityBuildPackage one = Prefab();
    error.clear();
    EXPECT_FALSE(ApplySpawnOverlay(one, overlay, Serializers, error));
    EXPECT_NE(error.find("not_here"), std::string::npos);
}
