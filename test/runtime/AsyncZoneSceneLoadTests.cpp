// The scene-driven zone load: BeginLoadScene resolves the cooked scene
// through the asset front door, stages and parses on the task thread, commits
// residency at the drain, and imports through the ordinary hidden-partition
// path. Zero-thread except the shared-residency case, which exercises real
// task threads reading one resident payload.

#include <assets/runtime/AssetSystem.h>
#include <assets/scene/SceneCache.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/AsyncZoneLoader.h>

#include "SmapSceneFixture.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct ZoneSceneMarker
{
    int Value = 0;
};

template <>
struct TypeSchema<ZoneSceneMarker>
{
    static constexpr std::string_view Name = "zone_scene_marker";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('Z', 'S', 'M', 'K');

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &ZoneSceneMarker::Value),
        };
    }
};

namespace
{
    struct SceneZoneHarness
    {
        explicit SceneZoneHarness(std::size_t workers = 0)
            : Tasks(workers)
            , Schema([] {
                WorldComponentSchema schema;
                schema.Add<ZoneSceneMarker>();
                schema.Seal();
                return schema;
            }())
            , World(Schema)
            , SceneContext(Logging)
            , Loader(Tasks, World, Schema, Serializers, SceneContext, Runtime)
            , Registry(Logging)
            , Scenes(Logging)
            , Assets(Logging, Registry, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, &Scenes, &Serializers)
        {
            EXPECT_EQ(Serializers.Register(
                          std::make_unique<ComponentSerializer<ZoneSceneMarker>>()),
                      ComponentSerializerRegistry::RegisterResult::Added);
        }

        LoggingProvider Logging;
        AsyncTaskQueue Tasks;
        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        RuntimeWorld World;
        SceneSerializationContext SceneContext;
        RuntimeFrameLoop Runtime;
        AsyncZoneLoader Loader;
        AssetRegistry Registry;
        SceneCache Scenes;
        AssetSystem Assets;
    };

    // A two-entity scene with one collision cell.
    [[nodiscard]] SmapContents MakeZoneContents()
    {
        SmapContents contents;
        for (int i = 0; i < 2; ++i)
        {
            SmapEntityRecord record;
            record.Components.emplace_back(
                MakeComponentTypeId("zone_scene_marker"),
                JsonValue(JsonValue::Object{
                    { "value", JsonValue(static_cast<double>(i)) } }));
            contents.Entities.push_back(std::move(record));
        }
        contents.Collision.push_back(
            SmapCollisionCell{ "levels/cell.scol", Vec3d(1.0f, 0.0f, 0.0f) });
        return contents;
    }

    std::size_t CountMarkers(RuntimeWorld& world, ZoneId zone)
    {
        const RuntimeZoneRecord* record = world.FindZone(zone);
        if (record == nullptr)
            return 0;
        std::size_t count = 0;
        for (EntityId entity : world.Entities().GetAliveEntities())
        {
            if (world.Entities().GetEntityPartition(entity) == record->Partition
                && world.Entities().TryGet<ZoneSceneMarker>(entity) != nullptr)
                ++count;
        }
        return count;
    }
} // namespace

TEST(AsyncZoneSceneLoad, ZeroThreadEndToEnd)
{
    SceneZoneHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeZoneContents(), "hall");

    std::size_t finalizeCollisionCells = 0;
    const ZoneId zone{ 5 };
    const AsyncTaskHandle handle = h.Loader.BeginLoadScene(
        zone, scene.Path, h.Assets, AsyncZoneLoader::SceneStageFn{},
        [&finalizeCollisionCells](RuntimeWorld&, RuntimeZoneRecord&,
                                  const SmapContents& contents) {
            finalizeCollisionCells = contents.Collision.size();
            return true;
        },
        ZoneParticipation{ .Logic = true });
    ASSERT_TRUE(handle.IsValid());
    EXPECT_TRUE(h.Loader.IsLoading(zone));

    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);

    ASSERT_TRUE(h.World.IsZoneResident(zone));
    EXPECT_EQ(CountMarkers(h.World, zone), 2u);
    EXPECT_EQ(finalizeCollisionCells, 1u);

    // The load's scene reference was scaffolding, released once publication
    // settled; nothing else holds the entry.
    EXPECT_FALSE(h.Scenes.Find(scene.Path).IsValid());
}

TEST(AsyncZoneSceneLoad, ResidentSceneLoadsWithoutTouchingTheFile)
{
    SceneZoneHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeZoneContents(), "pinned");

    // Pin the scene resident, then delete the backing file: a load that still
    // succeeds provably never re-read or re-parsed it.
    const SceneHandle pinned = h.Assets.LoadScene(scene.Path);
    ASSERT_TRUE(pinned.IsValid());
    std::filesystem::remove(scene.File);

    const ZoneId zone{ 6 };
    const AsyncTaskHandle handle = h.Loader.BeginLoadScene(
        zone, scene.Path, h.Assets, AsyncZoneLoader::SceneStageFn{},
        AsyncZoneLoader::SceneFinalizeFn{}, ZoneParticipation{ .Logic = true });
    ASSERT_TRUE(handle.IsValid());
    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);

    ASSERT_TRUE(h.World.IsZoneResident(zone));
    EXPECT_EQ(CountMarkers(h.World, zone), 2u);

    // The pin still holds the entry; the load's own reference is gone.
    EXPECT_TRUE(h.Scenes.Find(scene.Path).IsValid());
    h.Assets.ReleaseScene(pinned);
    EXPECT_FALSE(h.Scenes.Find(scene.Path).IsValid());
}

TEST(AsyncZoneSceneLoad, MissingSceneFileRecordsABuildFailure)
{
    SceneZoneHarness h;
    EXPECT_TRUE(h.Registry.Register(AssetRecord{
        .Type = AssetType::Scene,
        .SourceKind = AssetSourceKind::File,
        .Path = "asset://levels/gone.smap",
        .FilePath = "does/not/exist.smap",
    }));

    const ZoneId zone{ 7 };
    const AsyncTaskHandle handle = h.Loader.BeginLoadScene(
        zone, "asset://levels/gone.smap", h.Assets,
        AsyncZoneLoader::SceneStageFn{}, AsyncZoneLoader::SceneFinalizeFn{},
        ZoneParticipation{ .Logic = true });
    ASSERT_TRUE(handle.IsValid());
    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);

    EXPECT_FALSE(h.World.IsZoneResident(zone));
    const ZoneLoadFailure* failure = h.Loader.FindFailure(zone);
    ASSERT_NE(failure, nullptr);
    EXPECT_EQ(failure->Stage, ZoneLoadStage::Build);
    EXPECT_FALSE(h.Loader.IsLoading(zone));
}

TEST(AsyncZoneSceneLoad, ThreadedLoadsShareOneResidentSceneAcrossZones)
{
    SceneZoneHarness h(1);
    TempSmapScene scene(h.Registry, h.Serializers, MakeZoneContents(), "shared");

    // Pinned resident up front: both loads read the one shared payload from
    // their task threads while the owner thread keeps working the cache.
    const SceneHandle pinned = h.Assets.LoadScene(scene.Path);
    ASSERT_TRUE(pinned.IsValid());

    const ZoneId first{ 21 };
    const ZoneId second{ 22 };
    ASSERT_TRUE(h.Loader
                    .BeginLoadScene(first, scene.Path, h.Assets,
                                    AsyncZoneLoader::SceneStageFn{},
                                    AsyncZoneLoader::SceneFinalizeFn{},
                                    ZoneParticipation{ .Logic = true })
                    .IsValid());
    ASSERT_TRUE(h.Loader
                    .BeginLoadScene(second, scene.Path, h.Assets,
                                    AsyncZoneLoader::SceneStageFn{},
                                    AsyncZoneLoader::SceneFinalizeFn{},
                                    ZoneParticipation{ .Logic = true })
                    .IsValid());

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!h.World.IsZoneResident(first) || !h.World.IsZoneResident(second))
    {
        h.Tasks.DrainCompletions();
        ASSERT_LT(std::chrono::steady_clock::now(), deadline)
            << "zone loads timed out";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(CountMarkers(h.World, first), 2u);
    EXPECT_EQ(CountMarkers(h.World, second), 2u);
    h.Assets.ReleaseScene(pinned);
    EXPECT_FALSE(h.Scenes.Find(scene.Path).IsValid());
}

TEST(AsyncZoneSceneLoad, UnresolvedPathRefusesUpFront)
{
    SceneZoneHarness h;
    const ZoneId zone{ 8 };
    const AsyncTaskHandle handle = h.Loader.BeginLoadScene(
        zone, "asset://levels/never_registered.smap", h.Assets,
        AsyncZoneLoader::SceneStageFn{}, AsyncZoneLoader::SceneFinalizeFn{},
        ZoneParticipation{ .Logic = true });

    EXPECT_FALSE(handle.IsValid());
    EXPECT_FALSE(h.Loader.IsLoading(zone));
    const ZoneLoadFailure* failure = h.Loader.FindFailure(zone);
    ASSERT_NE(failure, nullptr);
    EXPECT_EQ(failure->Stage, ZoneLoadStage::Build);
}
