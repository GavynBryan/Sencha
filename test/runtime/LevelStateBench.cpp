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
#include <span>
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

        static EntityBuildPackage Props(std::size_t count, const JsonValue& body)
        {
            const JsonValue collider = *JsonParse(R"({"shape":{"type":"box","half_extents":[0.5,0.5,0.5]}})");
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

        JsonValue DynamicBody() const
        {
            const IComponentSerializer* serializer = Serializers.FindByType(ResolveComponentTypeId<RigidBody>());
            EXPECT_NE(serializer, nullptr);
            return serializer != nullptr ? SavedDefaultRecord(*serializer).value_or(JsonValue()) : JsonValue();
        }

        void Push(std::size_t first, std::size_t count, int rep)
        {
            World& world = Runtime.Entities();
            const PersistentEntityIndex& index = world.GetResource<PersistentEntityIndex>();
            for (std::size_t i = first; i < first + count; ++i)
            {
                const EntityId prop = index.TryResolve(PersistentEntityId{ 0x1000 + i });
                world.TryGet<LocalTransform>(prop)->Value.Position.X += 0.25f;
                world.TryGet<RigidBody>(prop)->LinearVelocity = Vec3d{ 0.0f, static_cast<float>(rep + 1), 0.0f };
            }
        }

        void Move(std::span<const PersistentEntityId> gained, std::size_t count)
        {
            World& world = Runtime.Entities();
            const PersistentEntityIndex& index = world.GetResource<PersistentEntityIndex>();
            for (std::size_t i = 0; i < count && i < gained.size(); ++i)
                world.TryGet<LocalTransform>(index.TryResolve(gained[i]))->Value.Position.Y += 0.25f;
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

        std::size_t ImageBytes(std::size_t count)
        {
            ZoneStateStore& store = Runtime.Entities().GetResource<ZoneStateStore>();
            std::size_t bytes = 0;
            for (std::size_t i = 0; i < count; ++i)
                if (const ResidentEntityState resident = store.ResidentOf(Scope, PersistentEntityId{ 0x1000 + i });
                    resident.Image != nullptr)
                    bytes += sizeof(ZoneCaptureImage) + resident.Image->Data.capacity();
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
        const EntityBuildPackage package = LevelStateBenchWorld::Props(count, bench.DynamicBody());
        Recorder.Record(std::format("props.{}.import", count), "ms", bench.Import(package));
        Recorder.Record(std::format("props.{}.baseline_members", count), "count",
                        static_cast<double>(bench.BaselineMembers(count)));
        Recorder.Record(std::format("props.{}.baseline_text_bytes", count), "bytes",
                        static_cast<double>(bench.BaselineBytes(count)));
        Recorder.Record(std::format("props.{}.image_bytes", count), "bytes", static_cast<double>(bench.ImageBytes(count)));
        const ZoneStateStore& store = bench.Runtime.Entities().GetResource<ZoneStateStore>();
        std::vector<double> untouched;
        for (int rep = 0; rep < Reps(9); ++rep)
            untouched.push_back(bench.Capture());
        EXPECT_EQ(store.PatchOf(bench.Scope, PersistentEntityId{ 0x1000 }), nullptr);
        const double untouchedMs = Bench::Median(untouched);
        Recorder.Record(std::format("props.{}.capture_untouched", count), "ms", untouchedMs);
        Recorder.Record(std::format("props.{}.capture_untouched_per_entity", count), "us",
                        1000.0 * untouchedMs / static_cast<double>(count));

        std::vector<double> some;
        for (int rep = 0; rep < Reps(9); ++rep)
        {
            bench.Push(0, 100, rep);
            some.push_back(bench.Capture());
        }
        EXPECT_NE(store.PatchOf(bench.Scope, PersistentEntityId{ 0x1000 }), nullptr);
        Recorder.Record(std::format("props.{}.capture_100_changed", count), "ms", Bench::Median(some));

        std::vector<double> every;
        for (int rep = 0; rep < Reps(9); ++rep)
        {
            bench.Push(0, count, rep);
            every.push_back(bench.Capture());
        }
        const double everyMs = Bench::Median(every);
        Recorder.Record(std::format("props.{}.capture_all_changed", count), "ms", everyMs);
        Recorder.Record(std::format("props.{}.capture_all_changed_per_entity", count), "us",
                        1000.0 * everyMs / static_cast<double>(count));
    }

    void RecordPropsDetach(std::size_t count, std::size_t changed)
    {
        std::vector<double> captures;
        std::vector<double> detaches;
        for (int rep = 0; rep < Reps(9); ++rep)
        {
            LevelStateBenchWorld bench;
            const EntityBuildPackage package = LevelStateBenchWorld::Props(count, bench.DynamicBody());
            (void)bench.Import(package);
            bench.Push(0, changed, rep);
            const std::vector<EntityId> identified = bench.Identified();
            const auto start = Bench::Clock::now();
            CaptureZoneState(bench.Runtime.Entities(), bench.Scope, identified, ZoneCapture::Detach);
            captures.push_back(Bench::MillisecondsSince(start));

            LevelStateBenchWorld again;
            (void)again.Import(package);
            again.Push(0, changed, rep);
            const auto detachStart = Bench::Clock::now();
            again.Detach();
            detaches.push_back(Bench::MillisecondsSince(detachStart));
        }
        Recorder.Record(std::format("props.{}.detach_capture_{}_changed", count, changed), "ms", Bench::Median(captures));
        Recorder.Record(std::format("props.{}.detach_whole_{}_changed", count, changed), "ms", Bench::Median(detaches));
    }

    void RecordPropsAttach(std::size_t count, std::size_t patched)
    {
        LevelStateBenchWorld bench;
        const EntityBuildPackage package = LevelStateBenchWorld::Props(count, bench.DynamicBody());
        ZoneStateStore& store = bench.Runtime.Entities().GetResource<ZoneStateStore>();
        (void)bench.Import(package);
        bench.Push(0, patched, 0);
        bench.Detach();
        const std::vector<DurableLevelState> remembered = store.ExportDurable();
        ASSERT_EQ(remembered.size(), 1u);

        std::vector<double> plain;
        std::vector<double> withPatches;
        std::vector<double> unkept;
        for (int rep = 0; rep < Reps(9); ++rep)
        {
            store.Install({});
            plain.push_back(bench.Import(package));
            bench.Detach();
            store.Install(remembered);
            withPatches.push_back(bench.Import(package));
            bench.Detach();
            bench.Runtime.Entities().SetResource(ZoneStateSerialization{});
            store.Install({});
            unkept.push_back(bench.Import(package));
            bench.Detach();
            bench.Runtime.Entities().SetResource(ZoneStateSerialization{ .Serializers = &bench.Serializers, .Context = &bench.Context });
        }
        const double plainMs = Bench::Median(plain);
        const double patchedMs = Bench::Median(withPatches);
        const std::string name = std::format("props.{}.attach_{}_patched", count, patched);
        Recorder.Record(name + ".import_not_kept", "ms", Bench::Median(unkept));
        Recorder.Record(name + ".import_unpatched", "ms", plainMs);
        Recorder.Record(name + ".import_patched", "ms", patchedMs);
        Recorder.Record(name + ".per_patched_entity", "us", 1000.0 * (patchedMs - plainMs) / static_cast<double>(patched));
    }

    void RecordGainedChanged(std::size_t zoneSize, std::size_t gained, std::size_t changed)
    {
        LevelStateBenchWorld bench;
        const EntityBuildPackage package = LevelStateBenchWorld::Package(zoneSize);
        (void)bench.Import(package);
        const std::vector<PersistentEntityId> ids = bench.Gain(gained);
        (void)bench.Capture();
        std::vector<double> captures;
        for (int rep = 0; rep < Reps(9); ++rep)
        {
            bench.Move(ids, changed);
            captures.push_back(bench.Capture());
        }
        Recorder.Record(std::format("gained.{}_in_{}.capture_{}_changed", gained, zoneSize, changed), "ms",
                        Bench::Median(captures));
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
    RecordPropsDetach(5000, 100);
    RecordPropsAttach(5000, 500);
    RecordGainedChanged(500, 2000, 20);
    for (const std::size_t patched : { 50u, 500u })
        RecordAttach(500, patched);
    for (const std::size_t gained : { 50u, 500u })
        RecordGained(500, gained);
    const std::filesystem::path json(out);
    EXPECT_TRUE(Recorder.WriteJson(json));
    EXPECT_TRUE(Recorder.WriteCsv(std::filesystem::path(json).replace_extension(".csv")));
}
