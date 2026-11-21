#include <core/json/JsonStringify.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/ComponentPersistence.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/CharacterMovementSerializer.h>
#include <movement/LocomotionMode.h>
#include <movement/components/CharacterMovement.h>
#include <runtime/spawn/SceneSpawnOverlay.h>
#include <world/build/EntityBuildPackage.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

#include <memory>
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

struct OverlayPose
{
    float Lean = 0.0f;
};

template <>
struct TypeSchema<OverlayPose>
{
    static constexpr std::string_view Name = "overlay_pose";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('O', 'V', 'P', 'S');
    static auto Fields() { return std::tuple{ MakeField("lean", &OverlayPose::Lean) }; }
};

struct OverlayName
{
    int Value = 0;
};

template <>
struct TypeSchema<OverlayName>
{
    static constexpr std::string_view Name = "overlay_name";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('O', 'V', 'N', 'M');
    static auto Fields() { return std::tuple{ MakeField("value", &OverlayName::Value) }; }
};

struct OverlayStance
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(OverlayStance, "test.overlay_stance");

template <>
struct ComponentPersistenceOf<OverlayVitals>
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Saved;
};

template <>
struct ComponentPersistenceOf<OverlayRoster>
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Saved;
};

template <>
struct ComponentPersistenceOf<OverlayBadge>
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Saved;
};

template <>
struct ComponentPersistenceOf<OverlayName>
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Identity;
};

namespace
{
    struct ErrorSink final : ILogSink
    {
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            if (level == LogLevel::Error)
                Errors.emplace_back(message);
        }

        std::vector<std::string> Errors;
    };

    class UnwritableVitalsSerializer final : public IComponentSerializer
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
        bool Save(IWriteArchive&, EntityId, const World&, SceneSerializationContext&) const override { return false; }
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
        ComponentPersistence Persistence() const override { return Inner.Persistence(); }

    private:
        ComponentSerializer<OverlayVitals> Inner;
    };

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
        ComponentPersistence Persistence() const override { return Inner.Persistence(); }

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
            Entities.RegisterComponent<OverlayPose>();
            (void)Serializers.Register(std::make_unique<ComponentSerializer<OverlayPose>>());
            Entities.RegisterComponent<OverlayName>();
            (void)Serializers.Register(std::make_unique<ComponentSerializer<OverlayName>>());
        }

        static void Author(EntityBuildPackage& package, ComponentTypeId type, JsonValue record)
        {
            (void)package.SetSerializedComponent(PackageEntityId{ 0 }, type, std::move(record));
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

TEST_F(SceneSpawnOverlayTest, ABodysPatchNamesOnlyItsSavedComponents)
{
    EntityBuildPackage prefab = Prefab();
    Author(prefab, ResolveComponentTypeId<OverlayPose>(), JsonValue(JsonValue::Object{ { "lean", JsonValue(0.0) } }));
    Author(prefab, ResolveComponentTypeId<OverlayName>(), JsonValue(JsonValue::Object{ { "value", JsonValue(1.0) } }));
    EntityBuildPackage leaning = Prefab();
    Author(leaning, ResolveComponentTypeId<OverlayPose>(), JsonValue(JsonValue::Object{ { "lean", JsonValue(0.0) } }));

    const EntityId body = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(body, OverlayVitals{ .Current = 50.0f, .Max = 100.0f });
    Entities.AddComponent<OverlayPose>(body, OverlayPose{ .Lean = 0.5f });
    Entities.AddComponent<OverlayName>(body, OverlayName{ .Value = 9 });
    const SceneSpawnOverlay changed = CaptureSpawnOverlay(Entities, body, prefab, Serializers, Context);
    EXPECT_EQ(JsonStringify(changed.RootPatch), R"({"overlay_vitals":{"current":50}})")
        << "an authored component and an identity follow the prefab";

    const EntityId bare = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(bare, OverlayVitals{});
    const SceneSpawnOverlay removed = CaptureSpawnOverlay(Entities, bare, leaning, Serializers, Context);
    EXPECT_TRUE(removed.Empty()) << "an authored component the body lost is not a removal: "
                                 << JsonStringify(removed.RootPatch);
}

TEST_F(SceneSpawnOverlayTest, ABodySavedInAnotherMovementModeComesBackInTheDefault)
{
    GameplayTagRegistry& tags = Entities.AddResource<GameplayTagRegistry>();
    LocomotionModeRegistry& modes = Entities.AddResource<LocomotionModeRegistry>(tags);
    const LocomotionModeId free = modes.RegisterFree();
    const LocomotionModeId climbing = modes.Register<OverlayStance>("movement.mode.climbing");
    Entities.RegisterComponent<OverlayStance>();
    Entities.RegisterComponent<CharacterMovement>();
    (void)Serializers.Register(MakeCharacterMovementSerializer());
    EntityBuildPackage prefab = Prefab();
    Author(prefab, ResolveComponentTypeId<CharacterMovement>(), JsonValue(JsonValue::Object{}));

    const EntityId body = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(body, OverlayVitals{});
    Entities.AddComponent<CharacterMovement>(body, CharacterMovement{ .Mode = climbing });
    const SceneSpawnOverlay overlay = CaptureSpawnOverlay(Entities, body, prefab, Serializers, Context);
    EXPECT_TRUE(overlay.Empty()) << JsonStringify(overlay.RootPatch);

    std::string error;
    ASSERT_TRUE(ApplySpawnOverlay(prefab, overlay, Serializers, error, Retuned)) << error;
    const PackageComponent* record =
        prefab.FindComponent(PackageEntityId{ 0 }, ResolveComponentTypeId<CharacterMovement>());
    ASSERT_TRUE(record != nullptr && record->SerializedJson.has_value());
    const EntityId restored = Entities.CreateEntity();
    JsonReadArchive archive(*record->SerializedJson);
    ASSERT_TRUE(Serializers.FindByType(ResolveComponentTypeId<CharacterMovement>())
                    ->LoadIntoWorld(archive, restored, Entities, Context));
    EXPECT_EQ(Entities.TryGet<CharacterMovement>(restored)->Mode, free);
}

TEST_F(SceneSpawnOverlayTest, ASavedComponentThatCannotBeWrittenIsLoggedAndFollowsThePrefab)
{
    ErrorSink& errors = Logging.AddSink<ErrorSink>();
    ComponentSerializerRegistry serializers;
    (void)serializers.Register(std::make_unique<UnwritableVitalsSerializer>());
    const EntityId body = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(body, OverlayVitals{ .Current = 50.0f, .Max = 100.0f });

    const SceneSpawnOverlay overlay = CaptureSpawnOverlay(Entities, body, Prefab(), serializers, Context);

    EXPECT_TRUE(overlay.Empty()) << JsonStringify(overlay.RootPatch);
    ASSERT_EQ(errors.Errors.size(), 1u);
    EXPECT_NE(errors.Errors[0].find("overlay_vitals"), std::string::npos) << errors.Errors[0];
}
