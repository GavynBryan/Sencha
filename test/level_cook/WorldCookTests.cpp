#include <gtest/gtest.h>

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/WorldCook.h"
#include "document/WorldDocument.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"

#include <assets/probes/ProbeVolumeFormat.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryReader.h>
#include <render/IrradianceVolumeComponent.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/WorldConnectionComponents.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace
{

class WorldCookTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_worldcook_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root / "materials/dev");
        std::ofstream(Root / "materials/dev/gray.smat", std::ios::trunc) << "{}";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    [[nodiscard]] std::string WorldPath() const { return (Root / "test.sworld").string(); }

    [[nodiscard]] static std::string ReadFile(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    [[nodiscard]] static WorldPartitionManifest ParseCookedManifest(const fs::path& path)
    {
        const auto json = JsonParse(ReadFile(path));
        EXPECT_TRUE(json.has_value());
        std::string error;
        const auto manifest = ReadWorldPartitionManifest(*json, &error);
        EXPECT_TRUE(manifest.has_value()) << error;
        return *manifest;
    }

    LoggingProvider Logging;   // sink-less: silent
    fs::path Root;
};

} // namespace

TEST_F(WorldCookTest, CooksTwoZoneWorldToCookedManifest)
{
    ZoneId first{};
    ZoneId second{};
    {
        WorldDocument world(Logging);
        world.NewWorld("TestWorld");
        first = world.Manifest().Zones[0].Id;
        second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
        ASSERT_TRUE(world.SetZoneBounds(second,
            Aabb3d::FromMinMax(Vec3d{ 24, 0, -5 }, Vec3d{ 40, 4, 5 })));
        world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
        ASSERT_TRUE(world.SetFocusZone(second));
        world.FocusDocument().GetScene().CreateBrush(Vec3d{ 32, 0, 0 });
        ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

        const WorldCookResult cooked = CookWorld(world, Root, 16.0, Logging, nullptr);
        ASSERT_TRUE(cooked.Success) << cooked.Error;
        EXPECT_EQ(cooked.ZoneCount, 2u);

        const WorldPartitionManifest manifest = ParseCookedManifest(cooked.CookedManifestPath);
        ASSERT_EQ(manifest.Zones.size(), 2u);
        for (const ZoneHeader& zone : manifest.Zones)
        {
            EXPECT_NE(zone.CookedContentHash, 0u);
            EXPECT_FALSE(zone.CookedSceneRef.empty());
            EXPECT_TRUE(fs::exists(Root / zone.CookedSceneRef));
            EXPECT_TRUE(fs::exists(Root / zone.CookedCollisionRef));
        }
        EXPECT_EQ(manifest.Zones[0].Id, first);
        EXPECT_EQ(manifest.Zones[1].Id, second);
    }
}

TEST_F(WorldCookTest, RefusesGateBindingToMissingDock)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    EditorDocument& worldScene = world.WorldSceneDocument();
    const EntityId gate = worldScene.GetScene().CreateEntity(Vec3d{});
    worldScene.GetScene().GetRegistry().Components.AddComponent(
        gate, DockGateBinding{ DockId{ 0xdead } });
    worldScene.MarkDirty();
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    const WorldCookResult cooked = CookWorld(world, Root, 16.0, Logging, nullptr);
    EXPECT_FALSE(cooked.Success);
    EXPECT_NE(cooked.Error.find("references missing dock"), std::string::npos);
}

TEST_F(WorldCookTest, RefusesLiveWorldValidationErrors)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
    ASSERT_TRUE(world.SetZoneBounds(second,
        Aabb3d::FromMinMax(Vec3d{}, Vec3d{})));
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    const WorldCookResult cooked = CookWorld(world, Root, 16.0, Logging, nullptr);
    EXPECT_FALSE(cooked.Success);
    EXPECT_NE(cooked.Error.find("partition.zone.bounds_invalid"), std::string::npos);
}

TEST_F(WorldCookTest, RecookWithoutEditsIsByteIdenticalAndEditsChangeOneHash)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
    ASSERT_TRUE(world.SetZoneBounds(second,
        Aabb3d::FromMinMax(Vec3d{ 24, 0, -5 }, Vec3d{ 40, 4, 5 })));
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    ASSERT_TRUE(world.SetFocusZone(second));
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 32, 0, 0 });
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    const WorldCookResult firstCook = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(firstCook.Success) << firstCook.Error;
    const WorldPartitionManifest before = ParseCookedManifest(firstCook.CookedManifestPath);

    std::vector<std::string> artifactsBefore;
    for (const ZoneHeader& zone : before.Zones)
        artifactsBefore.push_back(ReadFile(Root / zone.CookedSceneRef));

    // No edits: identical zone artifacts (id stability) and identical hashes.
    const WorldCookResult secondCook = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(secondCook.Success) << secondCook.Error;
    const WorldPartitionManifest unchanged = ParseCookedManifest(secondCook.CookedManifestPath);
    for (size_t i = 0; i < unchanged.Zones.size(); ++i)
    {
        EXPECT_EQ(unchanged.Zones[i].CookedContentHash, before.Zones[i].CookedContentHash);
        EXPECT_EQ(unchanged.Zones[i].CookedSceneRef, before.Zones[i].CookedSceneRef);
        EXPECT_EQ(ReadFile(Root / unchanged.Zones[i].CookedSceneRef), artifactsBefore[i]);
    }

    // Edit the second zone only: its hash changes, the first zone's does not.
    // Editor commands set the dirty flag; a direct scene mutation must mark it
    // so the save actually rewrites the zone scene.
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 40, 0, 0 });
    world.FocusDocument().MarkDirty();
    ASSERT_TRUE(world.SaveWorld());
    const WorldCookResult thirdCook = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(thirdCook.Success) << thirdCook.Error;
    const WorldPartitionManifest edited = ParseCookedManifest(thirdCook.CookedManifestPath);

    EXPECT_EQ(edited.Zones[0].CookedContentHash, before.Zones[0].CookedContentHash);
    EXPECT_NE(edited.Zones[1].CookedContentHash, before.Zones[1].CookedContentHash);
    EXPECT_EQ(edited.Zones[0].CookedSceneRef, before.Zones[0].CookedSceneRef);
    EXPECT_NE(edited.Zones[1].CookedSceneRef, before.Zones[1].CookedSceneRef);
    EXPECT_EQ(ReadFile(Root / edited.Zones[0].CookedSceneRef), artifactsBefore[0]);
}

TEST_F(WorldCookTest, CookReflectsCrossZoneMove)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
    ASSERT_TRUE(world.SetZoneBounds(second,
        Aabb3d::FromMinMax(Vec3d{ 24, 0, -5 }, Vec3d{ 40, 4, 5 })));
    ASSERT_TRUE(world.LoadZone(second));
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    // Far from the first brush: its geometry cooks to its own cell, so the move
    // shifts exactly one cell entity between the zones' cooked scenes.
    const EntityId moving = world.FocusDocument().GetScene().CreateBrush(Vec3d{ 777, 0, 0 });
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    const auto cookedEntityCount = [this](const std::string& sceneRef)
    {
        const auto json = JsonParse(ReadFile(Root / sceneRef));
        EXPECT_TRUE(json.has_value());
        const JsonValue* entities = json->Find("entities");
        EXPECT_NE(entities, nullptr);
        return entities->AsArray().size();
    };

    const WorldCookResult before = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(before.Success) << before.Error;
    const WorldPartitionManifest beforeManifest = ParseCookedManifest(before.CookedManifestPath);
    EXPECT_EQ(cookedEntityCount(beforeManifest.Zones[0].CookedSceneRef), 2u);
    EXPECT_EQ(cookedEntityCount(beforeManifest.Zones[1].CookedSceneRef), 0u);

    const EntityId entities[] = { moving };
    MoveEntitiesToZoneCommand move(entities, world.FocusDocument(),
                                   *world.ZoneDocument(second));
    move.Execute();
    ASSERT_TRUE(world.SaveWorld());

    const WorldCookResult after = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(after.Success) << after.Error;
    const WorldPartitionManifest afterManifest = ParseCookedManifest(after.CookedManifestPath);

    // Both zones changed content, and the moved geometry crossed over.
    EXPECT_NE(afterManifest.Zones[0].CookedContentHash, beforeManifest.Zones[0].CookedContentHash);
    EXPECT_NE(afterManifest.Zones[1].CookedContentHash, beforeManifest.Zones[1].CookedContentHash);
    EXPECT_EQ(cookedEntityCount(afterManifest.Zones[0].CookedSceneRef), 1u);
    EXPECT_EQ(cookedEntityCount(afterManifest.Zones[1].CookedSceneRef), 1u);
}

TEST_F(WorldCookTest, CooksWorldSceneBesideZones)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    world.WorldSceneDocument().GetScene().CreateBrush(Vec3d{ 64, 0, 0 });
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    const WorldCookResult cooked = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(cooked.Success) << cooked.Error;

    const WorldPartitionManifest manifest = ParseCookedManifest(cooked.CookedManifestPath);
    EXPECT_EQ(manifest.WorldSceneRef, "levels/test_world.level.json");
    ASSERT_FALSE(manifest.CookedWorldSceneRef.empty());
    ASSERT_FALSE(manifest.CookedWorldCollisionRef.empty());
    EXPECT_NE(manifest.CookedWorldContentHash, 0u);
    EXPECT_TRUE(fs::exists(Root / manifest.CookedWorldSceneRef));
    EXPECT_TRUE(fs::exists(Root / manifest.CookedWorldCollisionRef));
}

TEST_F(WorldCookTest, WorldSceneRecookIsByteIdenticalAndEditChangesOnlyItsHash)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    world.WorldSceneDocument().GetScene().CreateBrush(Vec3d{ 64, 0, 0 });
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    const WorldCookResult firstCook = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(firstCook.Success) << firstCook.Error;
    const WorldPartitionManifest before = ParseCookedManifest(firstCook.CookedManifestPath);
    const std::string artifactBefore = ReadFile(Root / before.CookedWorldSceneRef);

    // No edits: byte-identical world scene artifact, identical hash.
    const WorldCookResult secondCook = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(secondCook.Success) << secondCook.Error;
    const WorldPartitionManifest unchanged = ParseCookedManifest(secondCook.CookedManifestPath);
    EXPECT_EQ(unchanged.CookedWorldContentHash, before.CookedWorldContentHash);
    EXPECT_EQ(ReadFile(Root / unchanged.CookedWorldSceneRef), artifactBefore);

    // A world-scene edit changes its own hash and no zone's.
    world.WorldSceneDocument().GetScene().CreateBrush(Vec3d{ 96, 0, 0 });
    world.WorldSceneDocument().MarkDirty();
    ASSERT_TRUE(world.SaveWorld());
    const WorldCookResult thirdCook = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(thirdCook.Success) << thirdCook.Error;
    const WorldPartitionManifest edited = ParseCookedManifest(thirdCook.CookedManifestPath);

    EXPECT_NE(edited.CookedWorldContentHash, before.CookedWorldContentHash);
    ASSERT_EQ(edited.Zones.size(), before.Zones.size());
    for (size_t i = 0; i < edited.Zones.size(); ++i)
        EXPECT_EQ(edited.Zones[i].CookedContentHash, before.Zones[i].CookedContentHash);
}

namespace
{
// Sum of every c0 (ambient) coefficient across all channels of the first
// cooked probe volume: a scalar brightness proxy for occlusion assertions.
double ProbeC0Sum(const std::filesystem::path& cookedScenePath)
{
    std::string probePath = cookedScenePath.generic_string();
    constexpr std::string_view cookedSuffix = ".cooked.json";
    EXPECT_TRUE(probePath.ends_with(cookedSuffix));
    probePath.resize(probePath.size() - cookedSuffix.size());
    probePath += "/probes.sprobe";

    std::ifstream stream(probePath, std::ios::binary);
    EXPECT_TRUE(stream.is_open()) << probePath;
    BinaryReader reader(stream);
    ProbeVolumeFile file;
    EXPECT_TRUE(ReadProbeVolumeFile(reader, file));
    EXPECT_FALSE(file.Volumes.empty());

    double sum = 0.0;
    for (std::size_t i = 0; i < file.Volumes[0].ShHalf.size(); ++i)
        if (i % kProbeShCoefficients == kProbeShCoefficients - 1)
            sum += HalfToFloat(file.Volumes[0].ShHalf[i]);
    return sum;
}
} // namespace

TEST_F(WorldCookTest, NeighborZoneGeometryOccludesTheProbeBake)
{
    // Zone A authors a floor and a probe volume above it; zone B authors a
    // slab directly over the volume. Standalone, A's probes see open sky;
    // in the world cook, B's slab joins A's occlusion set through the halo.
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 },
                                                 Vec3d{ 4.0, 0.25, 4.0 });
    const EntityId volumeEntity =
        world.FocusDocument().GetScene().CreateEntity(Vec3d{ 0, 2, 0 });
    IrradianceVolumeComponent volume{};
    volume.HalfExtents = Vec3d(2.0f, 1.0f, 2.0f);
    volume.CellSize = 1.0f;
    world.FocusDocument().GetScene().GetRegistry().Components.AddComponent(
        volumeEntity, volume);
    ASSERT_TRUE(world.SetFocusZone(second));
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 6, 0 },
                                                 Vec3d{ 4.0, 0.25, 4.0 });
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    LightingCookParams params;
    params.Probe.SkyColor = Vec3d(1.0f, 1.0f, 1.0f);
    params.Probe.GroundColor = Vec3d(0.2f, 0.2f, 0.2f);

    const fs::path zoneScene =
        world.ResolveScenePath(world.Manifest().Zones[0].SceneRef);
    const DocumentCookResult standalone =
        CookDocument(zoneScene, Root, 16.0, &Logging, nullptr, params);
    ASSERT_TRUE(standalone.Success) << standalone.Error;
    const double open = ProbeC0Sum(standalone.CookedScenePath);
    ASSERT_GT(open, 0.0);

    // The halo changes the cook hash, so the world manifest switches this zone
    // to a different content-addressed artifact generation.
    const WorldCookResult cooked = CookWorld(world, Root, 16.0, Logging, nullptr, params);
    ASSERT_TRUE(cooked.Success) << cooked.Error;
    const WorldPartitionManifest manifest = ParseCookedManifest(cooked.CookedManifestPath);
    ASSERT_FALSE(manifest.Zones[0].CookedSceneRef.empty());
    const double blocked = ProbeC0Sum(Root / manifest.Zones[0].CookedSceneRef);

    EXPECT_GT(blocked, 0.0);
    EXPECT_LT(blocked, open * 0.9);
}

TEST_F(WorldCookTest, NeighborEditRestalesOnlyZonesWithinBakeReach)
{
    // Zone A has bake work (a probe volume), so neighbor halos fold into its
    // cook hash. "Near" sits within the default 100-unit bake reach; "Far"
    // sits beyond it.
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId near = world.AddZone(world.Manifest().Graphs[0].Id, "Near");
    const ZoneId far = world.AddZone(world.Manifest().Graphs[0].Id, "Far");
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 },
                                                 Vec3d{ 4.0, 0.25, 4.0 });
    const EntityId volumeEntity =
        world.FocusDocument().GetScene().CreateEntity(Vec3d{ 0, 2, 0 });
    IrradianceVolumeComponent volume{};
    volume.HalfExtents = Vec3d(2.0f, 1.0f, 2.0f);
    volume.CellSize = 1.0f;
    world.FocusDocument().GetScene().GetRegistry().Components.AddComponent(
        volumeEntity, volume);
    ASSERT_TRUE(world.SetFocusZone(near));
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 10, 0, 0 });
    ASSERT_TRUE(world.SetFocusZone(far));
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 500, 0, 0 });
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    const WorldCookResult firstCook = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(firstCook.Success) << firstCook.Error;
    const WorldPartitionManifest before = ParseCookedManifest(firstCook.CookedManifestPath);

    // An edit beyond reach: the probe zone's hash must not move.
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 505, 0, 0 });
    world.FocusDocument().MarkDirty();
    ASSERT_TRUE(world.SaveWorld());
    const WorldCookResult farCook = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(farCook.Success) << farCook.Error;
    const WorldPartitionManifest afterFar = ParseCookedManifest(farCook.CookedManifestPath);
    EXPECT_EQ(afterFar.Zones[0].CookedContentHash, before.Zones[0].CookedContentHash);
    EXPECT_NE(afterFar.Zones[2].CookedContentHash, before.Zones[2].CookedContentHash);

    // An edit within reach: the probe zone recooks against the new halo.
    ASSERT_TRUE(world.SetFocusZone(near));
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 12, 0, 0 });
    world.FocusDocument().MarkDirty();
    ASSERT_TRUE(world.SaveWorld());
    const WorldCookResult nearCook = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(nearCook.Success) << nearCook.Error;
    const WorldPartitionManifest afterNear = ParseCookedManifest(nearCook.CookedManifestPath);
    EXPECT_NE(afterNear.Zones[0].CookedContentHash, afterFar.Zones[0].CookedContentHash);
    EXPECT_NE(afterNear.Zones[1].CookedContentHash, before.Zones[1].CookedContentHash);
}

TEST_F(WorldCookTest, RefusesDirtyWorldScene)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    world.WorldSceneDocument().MarkDirty();
    const WorldCookResult cooked = CookWorld(world, Root, 16.0, Logging, nullptr);

    EXPECT_FALSE(cooked.Success);
    EXPECT_NE(cooked.Error.find("world scene"), std::string::npos);
}

TEST_F(WorldCookTest, RefusesDirtyZoneDocuments)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    world.FocusDocument().MarkDirty();
    const WorldCookResult cooked = CookWorld(world, Root, 16.0, Logging, nullptr);

    EXPECT_FALSE(cooked.Success);
    EXPECT_NE(cooked.Error.find("unsaved"), std::string::npos);
}
