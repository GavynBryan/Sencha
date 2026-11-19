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
#include <vector>

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

struct OverlayRoster
{
    int Value = 0;
};

template <>
struct TypeSchema<OverlayRoster>
{
    static constexpr std::string_view Name = "overlay_roster";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('O', 'V', 'R', 'S');
    static auto Fields() { return std::tuple{ MakeField("value", &OverlayRoster::Value) }; }
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
    class KeyedRosterSerializer final : public IComponentSerializer
    {
    public:
        ComponentTypeId TypeId() const override { return Inner.TypeId(); }
        std::string_view JsonKey() const override { return Inner.JsonKey(); }
        std::uint32_t BinaryChunkId() const override { return Inner.BinaryChunkId(); }
        std::span<const RuntimeField> RuntimeFields() const override { return Inner.RuntimeFields(); }
        std::vector<std::byte> DefaultBytes() const override { return Inner.DefaultBytes(); }
        void RegisterStorage(Registry& registry) const override { Inner.RegisterStorage(registry); }
        bool HasComponent(EntityId entity, const Registry& registry) const override
        {
            return Inner.HasComponent(entity, registry);
        }
        bool Save(IWriteArchive& archive, EntityId entity, const World& world,
                  SceneSerializationContext& context) const override
        {
            return Inner.Save(archive, entity, world, context);
        }
        bool Load(IReadArchive& archive, EntityId entity, Registry& registry,
                  SceneSerializationContext& context) override
        {
            return Inner.Load(archive, entity, registry, context);
        }
        bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                           SceneSerializationContext& context) override
        {
            return Inner.LoadIntoWorld(archive, entity, world, context);
        }
        bool Remove(EntityId entity, Registry& registry) const override { return Inner.Remove(entity, registry); }
        const DataFieldSchema* CompositionSchema() const override { return &Schema; }

    private:
        ComponentSerializer<OverlayRoster> Inner;
        DataFieldSchema Schema = MakeKeyedListRecordSchema("entries", "key");
    };

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
            Entities.RegisterComponent<OverlayRoster>();
            (void)Serializers.Register(std::make_unique<KeyedRosterSerializer>());
        }

        static EntityBuildPackage RosterPrefab()
        {
            EntityBuildPackage package;
            const PackageEntityId root = package.CreateEntity();
            JsonValue::Array entries{ JsonValue(JsonValue::Object{ { "key", JsonValue("a") } }) };
            (void)package.SetSerializedComponent(root, ResolveComponentTypeId<OverlayRoster>(),
                                                 JsonValue(JsonValue::Object{ { "entries", JsonValue(std::move(entries)) } }));
            return package;
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
        std::vector<std::string> Retuned;
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
    ASSERT_TRUE(ApplySpawnOverlay(restored, overlay, Serializers, error, Retuned)) << error;
    EXPECT_EQ(RootComponent(restored, ResolveComponentTypeId<OverlayVitals>()), R"({"current":50,"max":100})");
    EXPECT_EQ(RootComponent(restored, ResolveComponentTypeId<OverlayBadge>()), R"({"value":7})");
}

TEST_F(SceneSpawnOverlayTest, ACapturedPatchOverARetunedPrefabKeepsTheRetuneWhereTheBodyWasUntouched)
{
    const EntityId body = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(body, OverlayVitals{ .Current = 50.0f, .Max = 100.0f });
    const SceneSpawnOverlay overlay = CaptureSpawnOverlay(Entities, body, Prefab(), Serializers, Context);

    EntityBuildPackage retuned;
    const PackageEntityId root = retuned.CreateEntity();
    (void)retuned.SetSerializedComponent(
        root, ResolveComponentTypeId<OverlayVitals>(),
        JsonValue(JsonValue::Object{ { "current", JsonValue(150.0) }, { "max", JsonValue(150.0) } }));
    std::string error;
    ASSERT_TRUE(ApplySpawnOverlay(retuned, overlay, Serializers, error, Retuned)) << error;
    EXPECT_TRUE(Retuned.empty());
    EXPECT_EQ(RootComponent(retuned, ResolveComponentTypeId<OverlayVitals>()), R"({"current":50,"max":150})");
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
    ASSERT_TRUE(ApplySpawnOverlay(retuned, overlay, Serializers, error, Retuned)) << error;
    EXPECT_EQ(RootComponent(retuned, ResolveComponentTypeId<OverlayVitals>()), R"({"current":50,"max":150})");
}

TEST_F(SceneSpawnOverlayTest, APatchIsRefusedForASceneWithoutOneRootOrAComponentThisBuildLacks)
{
    SceneSpawnOverlay overlay;
    overlay.RootPatch = JsonValue(JsonValue::Object{
        { "overlay_vitals", JsonValue(JsonValue::Object{ { "current", JsonValue(1.0) } }) } });
    EntityBuildPackage twoRoots = Prefab(2);
    std::string error;
    EXPECT_FALSE(ApplySpawnOverlay(twoRoots, overlay, Serializers, error, Retuned));

    overlay.RootPatch = JsonValue(JsonValue::Object{ { "not_here", JsonValue(JsonValue::Object{}) } });
    EntityBuildPackage one = Prefab();
    error.clear();
    EXPECT_FALSE(ApplySpawnOverlay(one, overlay, Serializers, error, Retuned));
    EXPECT_NE(error.find("not_here"), std::string::npos);
}

TEST_F(SceneSpawnOverlayTest, APatchThatNoLongerFitsTheComponentsFormIsRefused)
{
    SceneSpawnOverlay overlay;
    overlay.RootPatch = JsonValue(JsonValue::Object{ { "overlay_roster",
        JsonValue(JsonValue::Object{ { "entries", JsonValue(JsonValue::Object{ { "a", JsonValue(5.0) } }) } }) } });
    EntityBuildPackage prefab = RosterPrefab();
    std::string error;
    EXPECT_FALSE(ApplySpawnOverlay(prefab, overlay, Serializers, error, Retuned));
    EXPECT_NE(error.find("element_not_a_record"), std::string::npos) << error;
}

TEST_F(SceneSpawnOverlayTest, WhatTheSceneNoLongerHasToRemoveIsSetAsideAndTheRestApplies)
{
    SceneSpawnOverlay overlay;
    overlay.RootPatch = JsonValue(JsonValue::Object{ { "overlay_roster",
        JsonValue(JsonValue::Object{ { "entries", JsonValue(JsonValue::Object{ { "gone", JsonValue() } }) } }) } });
    EntityBuildPackage prefab = RosterPrefab();
    std::string error;
    ASSERT_TRUE(ApplySpawnOverlay(prefab, overlay, Serializers, error, Retuned)) << error;
    ASSERT_EQ(Retuned.size(), 1u);
    EXPECT_NE(Retuned[0].find("remove_unknown_key"), std::string::npos) << Retuned[0];
    EXPECT_EQ(RootComponent(prefab, ResolveComponentTypeId<OverlayRoster>()), R"({"entries":[{"key":"a"}]})");
}
