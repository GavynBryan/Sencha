#include <abilities/AbilityGrantsSerializer.h>
#include <abilities/AbilityKit.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <attributes/AttributeSetSerializer.h>
#include <controller/LookOrientation.h>
#include <core/json/JsonEquals.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/ComponentPersistence.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagContainerSerializer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventorySerializers.h>
#include <logic/Counter.h>
#include <logic/Responses.h>
#include <logic/ResponsesSerializer.h>
#include <movement/CharacterMovementSerializer.h>
#include <movement/LocomotionMode.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/HeldMotion.h>
#include <runtime/spawn/SceneSpawnOverlay.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/TypedComponentSerializer.h>
#include <zone/ZonePackageImporter.h>

#include <gtest/gtest.h>

#include <algorithm>
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

struct OverlaySlots
{
    std::int32_t A = 0;
    std::int32_t B = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(OverlaySlots, "test.overlay_slots");

struct OverlayMark
{
    int Level = 0;
    int Tint = 0;
};

template <>
struct TypeSchema<OverlayMark>
{
    static constexpr std::string_view Name = "overlay_mark";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('O', 'V', 'M', 'K');
    static auto Fields()
    {
        return std::tuple{ MakeField("level", &OverlayMark::Level), MakeField("tint", &OverlayMark::Tint) };
    }
};

template <>
struct ComponentPersistenceOf<OverlaySlots>
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Saved;
};

template <>
struct ComponentPersistenceOf<OverlayMark>
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Saved;
};

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

    struct WarningSink final : ILogSink
    {
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            if (level == LogLevel::Warning)
                Warnings.emplace_back(message);
        }

        std::vector<std::string> Warnings;
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

    class SlotsSerializer final : public TypedComponentSerializer<OverlaySlots>
    {
    public:
        std::string_view JsonKey() const override { return "overlay_slots"; }
        std::uint32_t BinaryChunkId() const override { return MakeFourCC('O', 'V', 'S', 'L'); }
        std::span<const RuntimeField> RuntimeFields() const override { return {}; }
        const DataFieldSchema* CompositionSchema() const override { return &Schema; }

        bool Save(IWriteArchive& archive, EntityId entity, const World& world,
                  SceneSerializationContext&) const override
        {
            const OverlaySlots* slots = world.IsRegistered<OverlaySlots>() ? world.TryGet<OverlaySlots>(entity) : nullptr;
            if (slots == nullptr)
                return true;
            archive.BeginObject(std::string_view{});
            archive.BeginArray(std::string_view{ "entries" }, 2);
            for (const auto& [key, value] : { std::pair{ "a", slots->A }, std::pair{ "b", slots->B } })
            {
                archive.BeginObject(std::string_view{});
                archive.Field(std::string_view{ "key" }, std::string_view{ key });
                archive.Field(std::string_view{ "n" }, value);
                archive.End();
            }
            archive.End();
            archive.End();
            return archive.Ok();
        }

        bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world, SceneSerializationContext&) override
        {
            OverlaySlots slots;
            std::size_t count = 0;
            archive.BeginObject(std::string_view{});
            archive.BeginArray(std::string_view{ "entries" }, count);
            for (std::size_t i = 0; i < count; ++i)
            {
                std::string key;
                std::int32_t value = 0;
                archive.BeginObject(std::string_view{});
                archive.Field(std::string_view{ "key" }, key);
                archive.Field(std::string_view{ "n" }, value);
                archive.End();
                (key == "a" ? slots.A : slots.B) = value;
            }
            archive.End();
            archive.End();
            return archive.Ok() && Place(world, entity, slots);
        }

    private:
        DataFieldSchema Schema = MakeKeyedListRecordSchema("entries", "key");
    };

    JsonValue SlotsRecord(std::int32_t a, std::int32_t b)
    {
        JsonValue::Array entries{ JsonValue(JsonValue::Object{ { "key", JsonValue("a") }, { "n", JsonValue(a) } }),
                                  JsonValue(JsonValue::Object{ { "key", JsonValue("b") }, { "n", JsonValue(b) } }) };
        return JsonValue(JsonValue::Object{ { "entries", JsonValue(std::move(entries)) } });
    }

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

        EntityId BodyGaining(IComponentSerializer& serializer, const JsonValue& record)
        {
            const EntityId body = Entities.CreateEntity();
            Entities.AddComponent<OverlayVitals>(body, OverlayVitals{});
            JsonReadArchive archive(record);
            EXPECT_TRUE(serializer.LoadIntoWorld(archive, body, Entities, Context)) << JsonStringify(record);
            return body;
        }

        void ExpectGainKeptAsItsDifferenceFromItsDefault(std::unique_ptr<IComponentSerializer> made,
                                                         std::string_view changed, std::string_view patch)
        {
            IComponentSerializer& serializer = *made;
            ASSERT_EQ(Serializers.Register(std::move(made)), ComponentSerializerRegistry::RegisterResult::Added);
            JsonWriteArchive defaults;
            ASSERT_TRUE(serializer.SaveDefault(defaults)) << serializer.JsonKey() << " writes no default record";

            const EntityId idle = BodyGaining(serializer, defaults.TakeValue());
            const SceneSpawnOverlay untouched = CaptureSpawnOverlay(Entities, idle, Prefab(), Serializers, Context);
            EXPECT_TRUE(untouched.Empty()) << "a gained component at its default writes nothing: "
                                           << JsonStringify(untouched.RootPatch);

            const EntityId changedBody = BodyGaining(serializer, *JsonParse(changed));
            const SceneSpawnOverlay overlay = CaptureSpawnOverlay(Entities, changedBody, Prefab(), Serializers, Context);
            EXPECT_EQ(JsonStringify(overlay.RootPatch), patch);

            EntityBuildPackage restored = Prefab();
            std::string error;
            ASSERT_TRUE(ApplySpawnOverlay(restored, overlay, Serializers, error, Retuned)) << error;
            JsonWriteArchive live;
            ASSERT_TRUE(serializer.Save(live, changedBody, Entities, Context));
            const JsonValue held = live.TakeValue();
            EXPECT_TRUE(JsonEquals(*JsonParse(RootComponent(restored, serializer.TypeId())), held))
                << "composed over the default, the patch gives back what the body held: "
                << RootComponent(restored, serializer.TypeId()) << " against " << JsonStringify(held);
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
    EXPECT_EQ(JsonStringify(overlay.RootPatch), R"({"overlay_badge":{"value":7},"overlay_vitals":{"current":50}})");

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

namespace
{
    struct ModeRoundTrip
    {
        SceneSpawnOverlay Overlay;
        LocomotionModeId Restored;
    };
}

class SceneSpawnOverlayModeTest : public SceneSpawnOverlayTest
{
protected:
    SceneSpawnOverlayModeTest()
        : Modes(Entities.AddResource<LocomotionModeRegistry>(Entities.AddResource<GameplayTagRegistry>()))
    {
        Free = Modes.RegisterFree();
        Climbing = Modes.Register<OverlayStance>("movement.mode.climbing");
        Entities.RegisterComponent<OverlayStance>();
        Entities.RegisterComponent<CharacterMovement>();
        Entities.RegisterComponent<LocomotionModeLease>();
        (void)Serializers.Register(MakeCharacterMovementSerializer());
    }

    ModeRoundTrip RoundTrip(bool leased)
    {
        EntityBuildPackage prefab = Prefab();
        Author(prefab, ResolveComponentTypeId<CharacterMovement>(), JsonValue(JsonValue::Object{}));
        const EntityId body = Entities.CreateEntity();
        Entities.AddComponent<OverlayVitals>(body, OverlayVitals{});
        Entities.AddComponent<CharacterMovement>(body, CharacterMovement{ .Mode = Climbing });
        if (leased)
            *Entities.TryGet<LocomotionModeLease>(body) =
                LocomotionModeLease{ .Owner = LeaseOwner{ .Entity = body, .Key = 1 }, .Mode = Climbing };
        ModeRoundTrip trip{ .Overlay = CaptureSpawnOverlay(Entities, body, prefab, Serializers, Context), .Restored = {} };

        std::string error;
        EXPECT_TRUE(ApplySpawnOverlay(prefab, trip.Overlay, Serializers, error, Retuned)) << error;
        const PackageComponent* record =
            prefab.FindComponent(PackageEntityId{ 0 }, ResolveComponentTypeId<CharacterMovement>());
        EXPECT_TRUE(record != nullptr && record->SerializedJson.has_value());
        if (record == nullptr || !record->SerializedJson.has_value())
            return trip;
        const EntityId restored = Entities.CreateEntity();
        JsonReadArchive archive(*record->SerializedJson);
        EXPECT_TRUE(Serializers.FindByType(ResolveComponentTypeId<CharacterMovement>())
                        ->LoadIntoWorld(archive, restored, Entities, Context));
        trip.Restored = Entities.TryGet<CharacterMovement>(restored)->Mode;
        return trip;
    }

    LocomotionModeRegistry& Modes;
    LocomotionModeId Free;
    LocomotionModeId Climbing;
};

TEST_F(SceneSpawnOverlayModeTest, ABodySavedInAModeItIsSimplyInComesBackInIt)
{
    const ModeRoundTrip trip = RoundTrip(false);
    EXPECT_EQ(JsonStringify(trip.Overlay.RootPatch), R"({"CharacterMovement":{"mode":"movement.mode.climbing"}})");
    EXPECT_EQ(trip.Restored, Climbing);
}

TEST_F(SceneSpawnOverlayModeTest, ABodySavedInAModeALeaseHoldsComesBackInTheDefault)
{
    const ModeRoundTrip trip = RoundTrip(true);
    EXPECT_TRUE(trip.Overlay.Empty()) << JsonStringify(trip.Overlay.RootPatch);
    EXPECT_EQ(trip.Restored, Free);
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

TEST_F(SceneSpawnOverlayTest, ABodysPatchIsInKeyOrderWithItsRemovalAndAGainStatesOnlyWhatDiffersFromItsDefault)
{
    Entities.RegisterComponent<OverlaySlots>();
    (void)Serializers.Register(std::make_unique<SlotsSerializer>());
    Entities.RegisterComponent<OverlayMark>();
    (void)Serializers.Register(std::make_unique<ComponentSerializer<OverlayMark>>());
    EntityBuildPackage prefab = Prefab();
    Author(prefab, ResolveComponentTypeId<OverlayBadge>(), JsonValue(JsonValue::Object{ { "value", JsonValue(3) } }));
    Author(prefab, ResolveComponentTypeId<OverlaySlots>(), SlotsRecord(1, 2));

    const EntityId body = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(body, OverlayVitals{ .Current = 50.0f, .Max = 100.0f });
    Entities.AddComponent<OverlaySlots>(body, OverlaySlots{ .A = 1, .B = 5 });
    Entities.AddComponent<OverlayMark>(body, OverlayMark{ .Level = 2, .Tint = 0 });
    const SceneSpawnOverlay overlay = CaptureSpawnOverlay(Entities, body, prefab, Serializers, Context);

    EXPECT_EQ(JsonStringify(overlay.RootPatch),
              R"({"overlay_badge":null,"overlay_mark":{"level":2},"overlay_slots":{"entries":{"b":{"n":5}}},)"
              R"("overlay_vitals":{"current":50}})");
}

TEST_F(SceneSpawnOverlayTest, APatchsMembersAreInKeyOrderWhateverOrderTheirSerializersRegistered)
{
    ComponentSerializerRegistry reversed;
    (void)reversed.Register(std::make_unique<ComponentSerializer<OverlayBadge>>());
    (void)reversed.Register(std::make_unique<ComponentSerializer<OverlayVitals>>());
    const EntityId body = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(body, OverlayVitals{ .Current = 50.0f, .Max = 100.0f });
    Entities.AddComponent<OverlayBadge>(body, OverlayBadge{ .Value = 7 });

    const SceneSpawnOverlay forward = CaptureSpawnOverlay(Entities, body, Prefab(), Serializers, Context);
    const SceneSpawnOverlay backward = CaptureSpawnOverlay(Entities, body, Prefab(), reversed, Context);

    EXPECT_EQ(JsonStringify(forward.RootPatch), JsonStringify(backward.RootPatch));
}

TEST_F(SceneSpawnOverlayTest, ABytesAuthoredComponentRemovedAtRunTimeStaysRemoved)
{
    EntityBuildPackage prefab = Prefab();
    ASSERT_TRUE(prefab.AddComponent(PackageEntityId{ 0 }, OverlayBadge{ .Value = 3 }));
    const EntityId body = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(body, OverlayVitals{});

    const SceneSpawnOverlay overlay = CaptureSpawnOverlay(Entities, body, prefab, Serializers, Context);
    EXPECT_EQ(JsonStringify(overlay.RootPatch), R"({"overlay_badge":null})");

    std::string error;
    ASSERT_TRUE(ApplySpawnOverlay(prefab, overlay, Serializers, error, Retuned)) << error;
    EXPECT_EQ(prefab.FindComponent(PackageEntityId{ 0 }, ResolveComponentTypeId<OverlayBadge>()), nullptr);
}

TEST_F(SceneSpawnOverlayTest, ASavedComponentThePrefabLacksIsKeptAsItsDifferenceFromItsDefault)
{
    Entities.RegisterComponent<OverlayMark>();
    (void)Serializers.Register(std::make_unique<ComponentSerializer<OverlayMark>>());
    const EntityId idle = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(idle, OverlayVitals{});
    Entities.AddComponent<OverlayMark>(idle, OverlayMark{});
    const EntityId tinted = Entities.CreateEntity();
    Entities.AddComponent<OverlayVitals>(tinted, OverlayVitals{});
    Entities.AddComponent<OverlayMark>(tinted, OverlayMark{ .Level = 0, .Tint = 4 });

    EXPECT_TRUE(CaptureSpawnOverlay(Entities, idle, Prefab(), Serializers, Context).Empty())
        << "a gained component at its default writes nothing";
    const SceneSpawnOverlay overlay = CaptureSpawnOverlay(Entities, tinted, Prefab(), Serializers, Context);
    EXPECT_EQ(JsonStringify(overlay.RootPatch), R"({"overlay_mark":{"tint":4}})");

    EntityBuildPackage restored = Prefab();
    std::string error;
    ASSERT_TRUE(ApplySpawnOverlay(restored, overlay, Serializers, error, Retuned)) << error;
    EXPECT_EQ(RootComponent(restored, ResolveComponentTypeId<OverlayMark>()), R"({"level":0,"tint":4})")
        << "composed over the default";
}

TEST_F(SceneSpawnOverlayTest, AGainedTagContainerIsKeptAsItsDifferenceFromNoTags)
{
    Entities.RegisterComponent<GameplayTagContainer>();
    (void)Entities.AddResource<GameplayTagRegistry>().RegisterTag("door.unsealed");
    ExpectGainKeptAsItsDifferenceFromItsDefault(MakeGameplayTagContainerSerializer(),
                                                R"({"tags":[{"name":"door.unsealed","stacks":1}]})",
                                                R"({"GameplayTags":{"tags":{"door.unsealed":{"name":"door.unsealed","stacks":1}}}})");
}

TEST_F(SceneSpawnOverlayTest, AGainedAttributeSetIsKeptAsItsDifferenceFromNoAttributes)
{
    Entities.RegisterComponent<AttributeSet>();
    (void)Entities.AddResource<AttributeRegistry>().RegisterAttribute("Health");
    ExpectGainKeptAsItsDifferenceFromItsDefault(MakeAttributeSetSerializer(),
                                                R"({"attributes":[{"name":"Health","base":40}]})",
                                                R"({"Attributes":{"attributes":{"Health":{"name":"Health","base":40}}}})");
}

TEST_F(SceneSpawnOverlayTest, AGainedGrantListIsKeptAsItsDifferenceFromNoGrants)
{
    RegisterAbilityKit(Entities);
    ExpectGainKeptAsItsDifferenceFromItsDefault(
        MakeAbilityGrantsSerializer(), R"({"grants":[{"key":"g_plain","ability":"asset://abilities/primary.sdata"}]})",
        R"({"AbilityGrants":{"grants":{"g_plain":{"key":"g_plain","ability":"asset://abilities/primary.sdata"}}}})");
}

TEST_F(SceneSpawnOverlayTest, AGainedResponsesIsKeptAsItsDifferenceFromNoBindings)
{
    Entities.RegisterComponent<Responses>();
    ExpectGainKeptAsItsDifferenceFromItsDefault(MakeResponsesSerializer(),
                                                R"({"bindings":{"walk":{"on":"sensor.stay","sequence":{"commands":[]}}}})",
                                                R"({"responses":{"bindings":{"walk":{"on":"sensor.stay","sequence":{"commands":[]}}}}})");
}

TEST_F(SceneSpawnOverlayTest, AGainedLookOrientationIsKeptAsItsDifferenceFromItsDefault)
{
    Entities.RegisterComponent<LookOrientation>();
    ExpectGainKeptAsItsDifferenceFromItsDefault(std::make_unique<ComponentSerializer<LookOrientation>>(),
                                                R"({"yaw":1.5})", R"({"LookOrientation":{"yaw":1.5}})");
}

TEST_F(SceneSpawnOverlayTest, AGainedContainerAtItsDefaultIsStillKeptWhole)
{
    Entities.RegisterComponent<ItemContainer>();
    std::unique_ptr<IComponentSerializer> made = MakeItemContainerSerializer();
    IComponentSerializer& serializer = *made;
    ASSERT_EQ(Serializers.Register(std::move(made)), ComponentSerializerRegistry::RegisterResult::Added);
    JsonWriteArchive defaults;
    EXPECT_FALSE(serializer.SaveDefault(defaults)) << "a container at its defaults is still a container";

    const EntityId body = BodyGaining(serializer, *JsonParse(R"({"capacity":0})"));
    const SceneSpawnOverlay overlay = CaptureSpawnOverlay(Entities, body, Prefab(), Serializers, Context);
    EXPECT_EQ(JsonStringify(overlay.RootPatch), R"({"ItemContainer":{"capacity":0}})");
}

TEST(SceneSpawnOverlayRestore, ABodyPatchNamingATagContentNoLongerDeclaresRestoresContentsValueWithAWarning)
{
    WorldComponentSchema schema;
    schema.Add<Counter>();
    schema.Seal();
    RuntimeWorld runtime(schema);
    World& world = runtime.Entities();
    const GameplayTagId kept = *world.AddResource<GameplayTagRegistry>().RegisterTag("count.kept");
    ComponentSerializerRegistry serializers;
    (void)serializers.Register(std::make_unique<ComponentSerializer<Counter>>());
    LoggingProvider logging;
    WarningSink& warnings = logging.AddSink<WarningSink>();
    SceneSerializationContext context(logging);

    EntityBuildPackage prefab;
    const PackageEntityId root = prefab.CreateEntity();
    (void)prefab.SetSerializedComponent(
        root, ResolveComponentTypeId<Counter>(),
        JsonValue(JsonValue::Object{ { "name", JsonValue("count.kept") }, { "value", JsonValue(0) } }));
    SceneSpawnOverlay overlay;
    overlay.RootPatch = JsonValue(JsonValue::Object{
        { "Counter", JsonValue(JsonValue::Object{ { "name", JsonValue("count.deleted") }, { "value", JsonValue(3) } }) } });
    std::string error;
    std::vector<std::string> retuned;
    ASSERT_TRUE(ApplySpawnOverlay(prefab, overlay, serializers, error, retuned)) << error;

    ZoneImportError imported;
    std::vector<EntityId> created;
    ASSERT_TRUE(ImportPackageIntoPartition(world, schema, prefab, StoragePartitionId::Default(), ZoneStateScope{}, serializers,
                                           context, &imported, &created))
        << imported.Message;
    ASSERT_EQ(created.size(), 1u);
    const Counter* counter = world.TryGet<Counter>(created.front());
    ASSERT_NE(counter, nullptr);
    EXPECT_EQ(counter->Name, kept);
    EXPECT_EQ(counter->Value, 0);
    EXPECT_TRUE(std::ranges::any_of(warnings.Warnings, [](const std::string& warning) {
        return warning.find("does not decode") != std::string::npos;
    })) << "the fallback is reported";
}
