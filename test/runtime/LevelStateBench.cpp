#include "BenchRecorder.h"

#include <abilities/AbilityKit.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/BaselineGameplayTags.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/InventoryComponents.h>
#include <logic/Responses.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace
{
    Bench::Recorder Recorder;
    int Reps(int fallback) { return Bench::RepsFromEnvironment("SENCHA_LEVEL_STATE_BENCH_REPS", fallback); }

    constexpr ZoneParticipation kLogic{ .Visible = false, .Physics = false, .Logic = true, .Audio = false };
    constexpr ZoneId kZone{ 0x77 };
    const LevelReference kLevel{ .Kind = LevelKind::World, .Name = "bench" };

    WorldComponentSchema SchemaWith(ComponentSerializerRegistry& serializers)
    {
        WorldComponentSchema schema;
        ComponentRegistrar registrar(&schema, &serializers, nullptr);
        RegisterEngineComponents(registrar);
        schema.Seal();
        return schema;
    }

    struct LevelStateBenchWorld
    {
        LevelStateBenchWorld()
            : Context(Logging)
            , Schema(SchemaWith(Serializers))
            , Runtime(Schema)
        {
            World& world = Runtime.Entities();
            RegisterAbilityKit(world);
            Unsealed = *world.GetResource<GameplayTagRegistry>().RegisterTag("bench.unsealed");
            (void)world.GetResource<GameplayTagRegistry>().RegisterTag("bench.door");
            (void)world.GetResource<AttributeRegistry>().RegisterAttribute("Health");
            (void)world.GetResource<AttributeRegistry>().RegisterAttribute("Armor");
            world.SetResource(ZoneStateSerialization{ .Serializers = &Serializers, .Context = &Context });
            Scope = world.GetResource<ZoneStateStore>().Scope(kLevel, kZone);
        }

        static EntityBuildPackage Package(std::size_t count)
        {
            const JsonValue tags = *JsonParse(R"({"tags":[{"name":"bench.door","stacks":1}]})");
            const JsonValue attributes =
                *JsonParse(R"({"attributes":[{"name":"Health","base":100},{"name":"Armor","base":5}]})");
            const JsonValue responses = *JsonParse(R"({"bindings":{"open":{"on":"bench.used","once":true}}})");
            EntityBuildPackage package;
            for (std::size_t i = 0; i < count; ++i)
            {
                const PackageEntityId entity = package.CreateEntity();
                const PersistentEntityId id{ 0x1000 + i };
                EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(entity, { id }));
                EXPECT_TRUE(package.SetPersistentId(entity, id));
                EXPECT_TRUE(package.SetSerializedComponent(entity, ResolveComponentTypeId<GameplayTagContainer>(), tags));
                EXPECT_TRUE(package.SetSerializedComponent(entity, ResolveComponentTypeId<AttributeSet>(), attributes));
                EXPECT_TRUE(package.SetSerializedComponent(entity, ResolveComponentTypeId<Responses>(), responses));
            }
            return package;
        }

        static EntityBuildPackage Props(std::size_t count)
        {
            const JsonValue collider = *JsonParse(R"({"shape":{"type":"box","half_extents":[0.5,0.5,0.5]}})");
            const JsonValue body = *JsonParse(R"({"motion":"dynamic"})");
            EntityBuildPackage package;
            for (std::size_t i = 0; i < count; ++i)
            {
                const PackageEntityId entity = package.CreateEntity();
                const PersistentEntityId id{ 0x1000 + i };
                EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(entity, { id }));
                EXPECT_TRUE(package.SetPersistentId(entity, id));
                const JsonValue placed = *JsonParse(std::format(
                    R"({{"local":{{"position":[{},0.5,{}],"rotation":[0,0,0,1],"scale":[1,1,1]}}}})",
                    static_cast<double>(i % 100) * 1.5, static_cast<double>(i / 100) * 1.5));
                EXPECT_TRUE(package.SetSerializedComponent(entity, ResolveComponentTypeId<LocalTransform>(), placed));
                EXPECT_TRUE(package.SetSerializedComponent(entity, ResolveComponentTypeId<Collider>(), collider));
                EXPECT_TRUE(package.SetSerializedComponent(entity, ResolveComponentTypeId<RigidBody>(), body));
            }
            return package;
        }

        double Import(const EntityBuildPackage& package)
        {
            ZoneImportError error;
            const auto start = Bench::Clock::now();
            const bool imported =
                ImportZonePackage(Runtime, Schema, kZone, package, Serializers, Context, kLogic, &error, Scope);
            const double elapsed = Bench::MillisecondsSince(start);
            EXPECT_TRUE(imported) << error.Message;
            Finish();
            return elapsed;
        }

        void Detach()
        {
            EXPECT_TRUE(Runtime.RequestDetach(kZone));
            Finish();
        }

        void Finish()
        {
            Runtime.FlushLifecycleRequests();
            (void)Runtime.BeginResidencyProcessing();
            Runtime.FinalizeResidencyProcessing();
        }

        std::vector<EntityId> Identified()
        {
            return IdentifiedEntitiesIn(Runtime.Entities(), Runtime.FindZone(kZone)->Partition);
        }

        double Capture()
        {
            const std::vector<EntityId> identified = Identified();
            const auto start = Bench::Clock::now();
            CaptureZoneState(Runtime.Entities(), Scope, identified, ZoneCapture::Live);
            return Bench::MillisecondsSince(start);
        }

        void Unseal(std::size_t count)
        {
            World& world = Runtime.Entities();
            const PersistentEntityIndex& index = world.GetResource<PersistentEntityIndex>();
            for (std::size_t i = 0; i < count; ++i)
                (void)GrantBaselineTag(world, index.TryResolve(PersistentEntityId{ 0x1000 + i }), Unsealed);
        }

        std::vector<PersistentEntityId> Gain(std::size_t count)
        {
            World& world = Runtime.Entities();
            const StoragePartitionId partition = Runtime.FindZone(kZone)->Partition;
            std::vector<PersistentEntityId> gained;
            for (std::size_t i = 0; i < count; ++i)
            {
                const EntityId pickup = world.CreateEntity(partition);
                LocalTransform placed{};
                placed.Value.Position = Vec3d{ static_cast<float>(i), 0.0f, 1.0f };
                world.AddComponent<LocalTransform>(pickup, placed);
                world.AddComponent<ItemPickup>(pickup, ItemPickup{ .Definition = {}, .Count = 0 });
                EXPECT_TRUE(KeepWithZone(world, pickup));
                gained.push_back(world.TryGet<PersistentIdComponent>(pickup)->Id);
            }
            return gained;
        }

        std::size_t BaselineMembers(std::size_t count)
        {
            const ZoneStateStore& store = Runtime.Entities().GetResource<ZoneStateStore>();
            std::size_t members = 0;
            for (std::size_t i = 0; i < count; ++i)
                if (const std::vector<AuthoredComponent>* baseline = store.BaselineOf(Scope, PersistentEntityId{ 0x1000 + i }))
                    members += baseline->size();
            return members;
        }

        std::size_t BaselineBytes(std::size_t count)
        {
            const ZoneStateStore& store = Runtime.Entities().GetResource<ZoneStateStore>();
            std::size_t bytes = 0;
            for (std::size_t i = 0; i < count; ++i)
                if (const std::vector<AuthoredComponent>* baseline = store.BaselineOf(Scope, PersistentEntityId{ 0x1000 + i }))
                    for (const AuthoredComponent& component : *baseline)
                        bytes += JsonStringify(component.Record).size();
            return bytes;
        }

        LoggingProvider Logging;
        SceneSerializationContext Context;
        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
        GameplayTagId Unsealed;
        ZoneStateScope Scope;
    };

    void RecordCapture(std::size_t count)
    {
        LevelStateBenchWorld bench;
        const EntityBuildPackage package = LevelStateBenchWorld::Package(count);
        (void)bench.Import(package);
        Recorder.Record(std::format("zone.{}.baseline_text_bytes", count), "bytes",
                        static_cast<double>(bench.BaselineBytes(count)));

        std::vector<double> untouched;
        for (int rep = 0; rep < Reps(9); ++rep)
            untouched.push_back(bench.Capture());
        const double untouchedMs = Bench::Median(untouched);
        Recorder.Record(std::format("zone.{}.capture_untouched", count), "ms", untouchedMs);
        Recorder.Record(std::format("zone.{}.capture_untouched_per_entity", count), "us",
                        1000.0 * untouchedMs / static_cast<double>(count));

        bench.Unseal(count);
        std::vector<double> changed;
        for (int rep = 0; rep < Reps(9); ++rep)
            changed.push_back(bench.Capture());
        const double changedMs = Bench::Median(changed);
        Recorder.Record(std::format("zone.{}.capture_changed", count), "ms", changedMs);
        Recorder.Record(std::format("zone.{}.capture_changed_per_entity", count), "us",
                        1000.0 * changedMs / static_cast<double>(count));
        EXPECT_NE(bench.Runtime.Entities().GetResource<ZoneStateStore>().PatchOf(bench.Scope, PersistentEntityId{ 0x1000 }),
                  nullptr);
    }

    void RecordProps(std::size_t count)
    {
        LevelStateBenchWorld bench;
        const EntityBuildPackage package = LevelStateBenchWorld::Props(count);
        Recorder.Record(std::format("props.{}.import", count), "ms", bench.Import(package));
        Recorder.Record(std::format("props.{}.baseline_members", count), "count",
                        static_cast<double>(bench.BaselineMembers(count)));
        Recorder.Record(std::format("props.{}.baseline_text_bytes", count), "bytes",
                        static_cast<double>(bench.BaselineBytes(count)));
        std::vector<double> untouched;
        for (int rep = 0; rep < Reps(9); ++rep)
            untouched.push_back(bench.Capture());
        const double untouchedMs = Bench::Median(untouched);
        Recorder.Record(std::format("props.{}.capture_untouched", count), "ms", untouchedMs);
        Recorder.Record(std::format("props.{}.capture_untouched_per_entity", count), "us",
                        1000.0 * untouchedMs / static_cast<double>(count));
    }

    void RecordGained(std::size_t zoneSize, std::size_t gained)
    {
        LevelStateBenchWorld bench;
        const EntityBuildPackage package = LevelStateBenchWorld::Package(zoneSize);
        ZoneStateStore& store = bench.Runtime.Entities().GetResource<ZoneStateStore>();
        (void)bench.Import(package);
        std::vector<double> plain;
        for (int rep = 0; rep < Reps(9); ++rep)
            plain.push_back(bench.Capture());
        const std::vector<PersistentEntityId> ids = bench.Gain(gained);
        std::vector<double> withGained;
        for (int rep = 0; rep < Reps(9); ++rep)
            withGained.push_back(bench.Capture());
        EXPECT_EQ(store.GainedOf(bench.Scope).size(), gained);
        const double plainMs = Bench::Median(plain);
        const double gainedMs = Bench::Median(withGained);
        const std::string name = std::format("gained.{}_in_{}", gained, zoneSize);
        Recorder.Record(name + ".capture_none_gained", "ms", plainMs);
        Recorder.Record(name + ".capture", "ms", gainedMs);
        Recorder.Record(name + ".capture_per_gained", "us", 1000.0 * (gainedMs - plainMs) / static_cast<double>(gained));

        bench.Detach();
        const std::vector<DurableLevelState> remembered = store.ExportDurable();
        std::vector<double> bare;
        std::vector<double> made;
        for (int rep = 0; rep < Reps(9); ++rep)
        {
            store.Install({});
            bare.push_back(bench.Import(package));
            bench.Detach();
            store.Install(remembered);
            made.push_back(bench.Import(package));
            EXPECT_TRUE(bench.Runtime.Entities().GetResource<PersistentEntityIndex>().TryResolve(ids.front()).IsValid());
            bench.Detach();
        }
        const double bareMs = Bench::Median(bare);
        const double madeMs = Bench::Median(made);
        Recorder.Record(name + ".import_none_gained", "ms", bareMs);
        Recorder.Record(name + ".import", "ms", madeMs);
        Recorder.Record(name + ".import_per_gained", "us", 1000.0 * (madeMs - bareMs) / static_cast<double>(gained));
    }

    void RecordAttach(std::size_t zoneSize, std::size_t patched)
    {
        LevelStateBenchWorld bench;
        const EntityBuildPackage package = LevelStateBenchWorld::Package(zoneSize);
        ZoneStateStore& store = bench.Runtime.Entities().GetResource<ZoneStateStore>();

        (void)bench.Import(package);
        bench.Unseal(patched);
        bench.Detach();
        const std::vector<DurableLevelState> remembered = store.ExportDurable();
        ASSERT_EQ(remembered.size(), 1u);

        std::vector<double> plain;
        std::vector<double> withPatches;
        for (int rep = 0; rep < Reps(9); ++rep)
        {
            store.Install({});
            plain.push_back(bench.Import(package));
            bench.Detach();
            store.Install(remembered);
            withPatches.push_back(bench.Import(package));
            EXPECT_TRUE(std::as_const(bench.Runtime.Entities())
                            .TryGet<GameplayTagContainer>(
                                bench.Runtime.Entities().GetResource<PersistentEntityIndex>().TryResolve(
                                    PersistentEntityId{ 0x1000 }))
                            ->HasExact(bench.Unsealed));
            bench.Detach();
        }
        const double plainMs = Bench::Median(plain);
        const double patchedMs = Bench::Median(withPatches);
        Recorder.Record(std::format("attach.{}_of_{}.import_unpatched", patched, zoneSize), "ms", plainMs);
        Recorder.Record(std::format("attach.{}_of_{}.import_patched", patched, zoneSize), "ms", patchedMs);
        Recorder.Record(std::format("attach.{}_of_{}.per_patched_entity", patched, zoneSize), "us",
                        1000.0 * (patchedMs - plainMs) / static_cast<double>(patched));
    }
}

TEST(LevelStateBench, Generate)
{
    const char* out = std::getenv("SENCHA_LEVEL_STATE_BENCH_OUT");
    if (out == nullptr)
        GTEST_SKIP() << "set SENCHA_LEVEL_STATE_BENCH_OUT to record the level state bench";
    Recorder.Clear();
    for (const std::size_t count : { 500u, 5000u })
        RecordCapture(count);
    RecordProps(5000);
    for (const std::size_t patched : { 50u, 500u })
        RecordAttach(500, patched);
    for (const std::size_t gained : { 50u, 500u })
        RecordGained(500, gained);
    const std::filesystem::path json(out);
    EXPECT_TRUE(Recorder.WriteJson(json));
    EXPECT_TRUE(Recorder.WriteCsv(std::filesystem::path(json).replace_extension(".csv")));
}
