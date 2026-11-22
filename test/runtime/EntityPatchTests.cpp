#include "CompositionRoundTrip.h"

#include <core/composition/CompositionPatch.h>
#include <core/composition/JsonComposition.h>
#include <core/json/JsonEquals.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/ComponentPersistence.h>
#include <core/serialization/FourCC.h>
#include <ecs/World.h>
#include <world/build/EntityBuildPackage.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/EntityPatch.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/TypedComponentSerializer.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <vector>

struct PatchHealth
{
    float Current = 100.0f;
    float Max = 100.0f;
};

template <>
struct TypeSchema<PatchHealth>
{
    static constexpr std::string_view Name = "patch_health";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('P', 'H', 'L', 'T');
    static auto Fields()
    {
        return std::tuple{ MakeField("current", &PatchHealth::Current), MakeField("max", &PatchHealth::Max) };
    }
};

template <>
struct ComponentPersistenceOf<PatchHealth>
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Saved;
};

struct PatchTint
{
    int Value = 0;
};

template <>
struct TypeSchema<PatchTint>
{
    static constexpr std::string_view Name = "patch_tint";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('P', 'T', 'N', 'T');
    static auto Fields() { return std::tuple{ MakeField("value", &PatchTint::Value) }; }
};

struct PatchNote
{
    int Said = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(PatchNote, "test.patch_note");

struct RecordKindA
{
};
struct RecordKindB
{
};
struct RecordKindC
{
};

SENCHA_DECLARE_COMPONENT_TYPE(RecordKindA, "test.record_kind_a");
SENCHA_DECLARE_COMPONENT_TYPE(RecordKindB, "test.record_kind_b");
SENCHA_DECLARE_COMPONENT_TYPE(RecordKindC, "test.record_kind_c");

namespace
{
    JsonValue Parse(std::string_view text)
    {
        std::optional<JsonValue> parsed = JsonParse(text);
        EXPECT_TRUE(parsed.has_value()) << text;
        return parsed.value_or(JsonValue());
    }

    class CountingHealthSerializer final : public IComponentSerializer
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
            ++Saves;
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
        ComponentPersistence Persistence() const override { return Inner.Persistence(); }
        bool SaveDefault(IWriteArchive& archive) const override { return Inner.SaveDefault(archive); }

        mutable int Saves = 0;

    private:
        ComponentSerializer<PatchHealth> Inner;
    };

    class NoteSerializer final : public TypedComponentSerializer<PatchNote>
    {
    public:
        std::string_view JsonKey() const override { return "patch_note"; }
        std::uint32_t BinaryChunkId() const override { return MakeFourCC('P', 'N', 'O', 'T'); }
        std::span<const RuntimeField> RuntimeFields() const override { return {}; }

        bool Save(IWriteArchive& archive, EntityId entity, const World& world,
                  SceneSerializationContext& context) const override
        {
            const PatchNote* note = world.IsRegistered<PatchNote>() ? world.TryGet<PatchNote>(entity) : nullptr;
            if (note == nullptr)
                return true;
            archive.BeginObject(std::string_view{});
            archive.Field(std::string_view{ "said" }, static_cast<std::int32_t>(note->Said));
            if (context.Purpose == SceneRecordPurpose::Save)
                archive.Field(std::string_view{ "only_saved" }, true);
            archive.End();
            return archive.Ok();
        }

        bool LoadIntoWorld(IReadArchive&, EntityId, World&, SceneSerializationContext&) override { return false; }
    };

    class RecordSerializer final : public IComponentSerializer
    {
    public:
        RecordSerializer(ComponentTypeId type, std::string key, std::uint32_t chunk)
            : Type(type)
            , Key(std::move(key))
            , Chunk(chunk)
        {
        }

        ComponentTypeId TypeId() const override { return Type; }
        std::string_view JsonKey() const override { return Key; }
        std::uint32_t BinaryChunkId() const override { return Chunk; }
        std::span<const RuntimeField> RuntimeFields() const override { return {}; }
        std::vector<std::byte> DefaultBytes() const override { return {}; }
        void RegisterStorage(Registry&) const override {}
        bool HasComponent(EntityId, const Registry&) const override { return false; }
        bool Save(IWriteArchive&, EntityId, const World&, SceneSerializationContext&) const override { return true; }
        bool Load(IReadArchive&, EntityId, Registry&, SceneSerializationContext&) override { return false; }
        bool LoadIntoWorld(IReadArchive&, EntityId, World&, SceneSerializationContext&) override { return false; }
        bool Remove(EntityId, Registry&) const override { return true; }
        const DataFieldSchema* CompositionSchema() const override { return &Schema; }

    private:
        ComponentTypeId Type;
        std::string Key;
        std::uint32_t Chunk;
        DataFieldSchema Schema = CompositionRoundTrip::Schema();
    };

    class EntityPatchTest : public ::testing::Test
    {
    protected:
        EntityPatchTest()
            : Context(Logging)
        {
            Entities.RegisterComponent<PatchHealth>();
            Entities.RegisterComponent<PatchTint>();
            Entities.RegisterComponent<PatchNote>();
            auto health = std::make_unique<CountingHealthSerializer>();
            Health = health.get();
            (void)Serializers.Register(std::move(health));
            (void)Serializers.Register(std::make_unique<ComponentSerializer<PatchTint>>());
            (void)Serializers.Register(std::make_unique<NoteSerializer>());
        }

        static JsonValue HealthRecord(double current, double max)
        {
            return JsonValue(JsonValue::Object{ { "current", JsonValue(current) }, { "max", JsonValue(max) } });
        }

        static std::string Record(const PackageEntity& entity, ComponentTypeId type)
        {
            const PackageComponent* component = entity.Find(type);
            return component != nullptr && component->SerializedJson ? JsonStringify(*component->SerializedJson) : "";
        }

        LoggingProvider Logging;
        SceneSerializationContext Context;
        World Entities;
        ComponentSerializerRegistry Serializers;
        CountingHealthSerializer* Health = nullptr;
    };

    constexpr ComponentPersistenceSet kSaved{ .Saved = true };
}

TEST_F(EntityPatchTest, ANonRootPackageEntityIsPatchedAndApplied)
{
    EntityBuildPackage prefab;
    const PackageEntityId root = prefab.CreateEntity();
    const PackageEntityId child = prefab.CreateEntity();
    ASSERT_TRUE(prefab.SetParent(child, root));
    const ComponentTypeId health = ResolveComponentTypeId<PatchHealth>();
    (void)prefab.SetSerializedComponent(root, health, HealthRecord(100.0, 100.0));
    (void)prefab.SetSerializedComponent(child, health, HealthRecord(100.0, 100.0));
    const EntityId wounded = Entities.CreateEntity();
    Entities.AddComponent<PatchHealth>(wounded, PatchHealth{ .Current = 40.0f, .Max = 100.0f });

    const std::vector<AuthoredComponent> baseline =
        AuthoredComponentsOf(prefab.Entities()[child.Value], Serializers, { .Declared = kSaved });
    const JsonValue patch = CaptureEntityPatch(Entities, wounded, baseline, kSaved, Serializers, Context);
    EXPECT_EQ(JsonStringify(patch), R"({"patch_health":{"current":40}})");

    const EntityPatchOutcome outcome = ApplyEntityPatch(*prefab.FindEntity(child), patch, Serializers);
    EXPECT_TRUE(outcome.Retuned.empty() && outcome.Misfit.empty() && outcome.Unknown.empty());
    EXPECT_EQ(Record(prefab.Entities()[child.Value], health), R"({"current":40,"max":100})");
    EXPECT_EQ(Record(prefab.Entities()[root.Value], health), R"({"current":100,"max":100})");
    ASSERT_EQ(prefab.Entities()[child.Value].Unpatched.size(), 1u) << "content's record is kept for the importer";
    EXPECT_EQ(JsonStringify(*prefab.Entities()[child.Value].Unpatched.front().SerializedJson),
              R"({"current":100,"max":100})");
    EXPECT_TRUE(prefab.Entities()[root.Value].Unpatched.empty());
}

TEST_F(EntityPatchTest, ASubsetBaselineCapturesOnlyItsComponents)
{
    EntityBuildPackage prefab;
    const PackageEntityId root = prefab.CreateEntity();
    (void)prefab.SetSerializedComponent(root, ResolveComponentTypeId<PatchHealth>(), HealthRecord(100.0, 100.0));
    (void)prefab.SetSerializedComponent(root, ResolveComponentTypeId<PatchTint>(),
                                        JsonValue(JsonValue::Object{ { "value", JsonValue(1) } }));
    const EntityId crate = Entities.CreateEntity();
    Entities.AddComponent<PatchHealth>(crate, PatchHealth{ .Current = 10.0f, .Max = 100.0f });
    Entities.AddComponent<PatchTint>(crate, PatchTint{ .Value = 6 });

    const std::array listed{ ResolveComponentTypeId<PatchTint>() };
    const std::vector<AuthoredComponent> baseline =
        AuthoredComponentsOf(prefab.Entities()[root.Value], Serializers, { .Listed = listed });
    ASSERT_EQ(baseline.size(), 1u);
    const JsonValue patch = CaptureEntityPatch(Entities, crate, baseline, {}, Serializers, Context);

    EXPECT_EQ(JsonStringify(patch), R"({"patch_tint":{"value":6}})") << "the changed Saved health is not listed";
    EXPECT_EQ(Health->Saves, 0) << "a component the baseline does not list is never written";
}

TEST_F(EntityPatchTest, AMemberNamingAComponentThisBuildLacksIsCarriedThroughCaptureAndReportedByApply)
{
    EntityBuildPackage prefab;
    const PackageEntityId root = prefab.CreateEntity();
    const ComponentTypeId health = ResolveComponentTypeId<PatchHealth>();
    (void)prefab.SetSerializedComponent(root, health, HealthRecord(100.0, 100.0));
    const EntityId body = Entities.CreateEntity();
    Entities.AddComponent<PatchHealth>(body, PatchHealth{ .Current = 40.0f, .Max = 100.0f });
    const JsonValue kept = Parse(R"({ "zz_newer": null, "patch_health": { "current": 1 }, "aa_newer": { "x": 1 } })");

    const std::vector<AuthoredComponent> baseline =
        AuthoredComponentsOf(prefab.Entities()[root.Value], Serializers, { .Declared = kSaved });
    const JsonValue patch = CaptureEntityPatch(Entities, body, baseline, kSaved, Serializers, Context, &kept);
    EXPECT_EQ(JsonStringify(patch), R"({"aa_newer":{"x":1},"patch_health":{"current":40},"zz_newer":null})");

    const EntityPatchOutcome outcome = ApplyEntityPatch(*prefab.FindEntity(root), patch, Serializers);
    EXPECT_EQ(outcome.Unknown, (std::vector<std::string>{ "aa_newer", "zz_newer" }));
    EXPECT_TRUE(outcome.Misfit.empty());
    EXPECT_EQ(Record(prefab.Entities()[root.Value], health), R"({"current":40,"max":100})");
    EXPECT_EQ(prefab.Entities()[root.Value].Components.size(), 1u);
}

TEST_F(EntityPatchTest, TheRecordWriterIsToldItsPurpose)
{
    const EntityId record = Entities.CreateEntity();
    Entities.AddComponent<PatchNote>(record, PatchNote{ .Said = 2 });
    Entities.AddComponent<PatchTint>(record, PatchTint{ .Value = 6 });
    const EntityScope authored{ .Declared = { .Authored = true } };

    EXPECT_EQ(JsonStringify(WriteEntityRecord(Entities, record, Serializers, Context, authored,
                                              SceneRecordPurpose::Authoring)),
              R"({"patch_note":{"said":2},"patch_tint":{"value":6}})");
    EXPECT_EQ(JsonStringify(WriteEntityRecord(Entities, record, Serializers, Context, authored,
                                              SceneRecordPurpose::Save)),
              R"({"patch_note":{"said":2,"only_saved":true},"patch_tint":{"value":6}})");
    EXPECT_EQ(Context.Purpose, SceneRecordPurpose::Authoring) << "the purpose lasts only while the record is written";
}

TEST(EntityPatchRecords, EveryEntityRecordComposesBackFromItsPatch)
{
    ComponentSerializerRegistry serializers;
    const std::array<std::pair<std::string, ComponentTypeId>, 3> kinds{
        std::pair{ std::string("kind_c"), ResolveComponentTypeId<RecordKindC>() },
        std::pair{ std::string("kind_a"), ResolveComponentTypeId<RecordKindA>() },
        std::pair{ std::string("kind_b"), ResolveComponentTypeId<RecordKindB>() } };
    std::uint32_t chunk = MakeFourCC('R', 'K', 'D', '0');
    for (const auto& [key, type] : kinds)
        ASSERT_EQ(serializers.Register(std::make_unique<RecordSerializer>(type, key, chunk++)),
                  ComponentSerializerRegistry::RegisterResult::Added);
    const DataFieldSchema& entity = serializers.EntitySchema();
    ASSERT_EQ(entity.Children.size(), kinds.size());

    CompositionRoundTrip::Generator generator(20261121u);
    std::mt19937 shapes(7u);
    for (int i = 0; i < 1000; ++i)
    {
        JsonValue base(JsonValue::Object{});
        JsonValue target(JsonValue::Object{});
        PackageEntity content;
        for (const auto& [key, type] : kinds)
        {
            const CompositionRoundTrip::Case pair = generator.Next();
            const unsigned shape = shapes() % 4;
            if (shape != 2)
            {
                base.AsObject().emplace_back(key, Parse(pair.Base));
                content.SetSerialized(type, Parse(pair.Base));
            }
            if (shape != 1)
                target.AsObject().emplace_back(key, Parse(pair.Target));
        }
        const JsonValue patch = PatchFrom(base, target, &entity).value_or(JsonValue(JsonValue::Object{}));
        const CompositionLayerInput<JsonValue> under{ &base, "content", nullptr };
        const CompositionLayerInput<JsonValue> over{ &patch, "save", nullptr };
        CompositionReport report;
        const JsonValue composed = ComposeLayers<JsonValue>(&under, {}, over, { .Schema = &entity }, report);
        ASSERT_TRUE(report.Diagnostics.empty()) << JsonStringify(base) << "\n" << JsonStringify(target);
        ASSERT_TRUE(JsonEquals(composed, target))
            << "base   " << JsonStringify(base) << "\ntarget " << JsonStringify(target) << "\ngot    "
            << JsonStringify(composed);

        const EntityPatchOutcome outcome = ApplyEntityPatch(content, patch, serializers);
        ASSERT_TRUE(outcome.Retuned.empty() && outcome.Misfit.empty() && outcome.Unknown.empty());
        for (const auto& [key, type] : kinds)
        {
            const JsonValue* wanted = target.Find(key);
            const PackageComponent* applied = content.Find(type);
            ASSERT_EQ(applied != nullptr, wanted != nullptr) << key << " in " << JsonStringify(target);
            if (wanted != nullptr)
            {
                ASSERT_TRUE(JsonEquals(*applied->SerializedJson, *wanted))
                    << key << ": " << JsonStringify(*applied->SerializedJson) << " for " << JsonStringify(*wanted);
            }
        }
    }
}
