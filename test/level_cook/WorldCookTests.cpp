#include <gtest/gtest.h>

#include "document/DocumentCook.h"
#include "CookedSmapReaders.h"
#include "document/DocumentSerialization.h"
#include "document/WorldCook.h"
#include "scene_source/Json5Parser.h"
#include "scene_source/Json5Writer.h"
#include "document/WorldDocument.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"

#include <assets/probes/ProbeVolumeFormat.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryReader.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/PointLightComponent.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/identity/PersistentIdComponent.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/WorldConnectionComponents.h>

#include <filesystem>
#include <fstream>
#include <functional>
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

    // Rewrites a saved zone file through the scene-source layer, standing in
    // for content the editor cannot author: a file that predates persistent
    // identity, or one whose ids were written by hand.
    static void EditSavedScene(const fs::path& path,
                               const std::function<void(Json5Value&)>& edit)
    {
        Json5ParseError error;
        auto root = Json5Parse(ReadFile(path), &error);
        ASSERT_TRUE(root.has_value()) << path.generic_string() << ": " << error.Message;
        edit(*root);
        std::ofstream(path, std::ios::binary | std::ios::trunc) << Json5Write(*root);
    }

    static void StripPersistentIds(Json5Value& root)
    {
        // Identity lives at record level in .sscene.
        if (Json5Value* entities = root.FindMutable("entities"))
            for (Json5Value& record : entities->Elements)
                std::erase_if(record.Members, [](const Json5Value::Member& member)
                              { return member.first == "id"; });
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
        }
        EXPECT_EQ(manifest.Zones[0].Id, first);
        EXPECT_EQ(manifest.Zones[1].Id, second);
    }
}

// §3 at the world level: SaveWorld lands zone files on disk, then every open
// document that places one of them re-projects from the fresh content.
TEST_F(WorldCookTest, SavingAZoneReprojectsTheZoneThatPlacesIt)
{
    WorldDocument world(Logging);
    world.SetContentRoots({ Root });
    world.NewWorld("TestWorld");
    const ZoneId prefabZone = world.Manifest().Zones[0].Id;
    const ZoneId hostZone = world.AddZone(world.Manifest().Graphs[0].Id, "Host");
    ASSERT_TRUE(world.SetZoneBounds(hostZone,
        Aabb3d::FromMinMax(Vec3d{ 24, 0, -5 }, Vec3d{ 40, 4, 5 })));

    // The prefab zone: one light. Saved once so the host can resolve it.
    const EntityId light =
        world.FocusDocument().GetScene().CreateEntity(Vec3d{ 0, 2, 0 });
    PointLightComponent lamp{};
    lamp.Range = 3.0f;
    world.FocusDocument().GetScene().GetRegistry().Components.AddComponent(
        light, lamp);
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));
    const std::string prefabRef = world.Manifest().Zones[0].SceneRef;

    // The host zone places it.
    ASSERT_TRUE(world.SetFocusZone(hostZone));
    std::string placeError;
    const SceneInstanceId placed = world.FocusDocument().PlaceSceneInstance(
        "asset://" + prefabRef, Transform3f::Identity(), {}, &placeError);
    ASSERT_TRUE(placed.IsValid()) << placeError;
    const auto hostLightRange = [&]() -> float
    {
        for (EntityId entity : world.FocusDocument().GetScene().GetAllEntities())
            if (const auto* projected =
                    world.FocusDocument().GetRegistry()
                        .Components.TryGet<PointLightComponent>(entity))
                return projected->Range;
        return -1.0f;
    };
    ASSERT_EQ(hostLightRange(), 3.0f);

    // Edit the prefab zone and save the world: the host must show the new
    // value without a reload.
    ASSERT_TRUE(world.SetFocusZone(prefabZone));
    world.FocusDocument().GetScene().GetRegistry()
        .Components.TryGet<PointLightComponent>(light)->Range = 9.0f;
    world.FocusDocument().MarkDirty();
    ASSERT_TRUE(world.SetFocusZone(hostZone));
    ASSERT_TRUE(world.SaveWorld());

    EXPECT_EQ(hostLightRange(), 9.0f)
        << "the placing zone did not re-project after its source saved";
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

TEST_F(WorldCookTest, RefusesGateBindingInAnUnopenedZone)
{
    // Live validation only sees open zones, so the bad binding is authored,
    // saved, and then reached through a fresh load that leaves its zone closed.
    ZoneId second{};
    {
        WorldDocument world(Logging);
        world.NewWorld("TestWorld");
        const ZoneId first = world.Manifest().Zones[0].Id;
        second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
        ASSERT_TRUE(world.SetZoneBounds(second,
            Aabb3d::FromMinMax(Vec3d{ 24, 0, -5 }, Vec3d{ 40, 4, 5 })));
        ASSERT_TRUE(world.SetFocusZone(second));
        const EntityId gate = world.FocusDocument().GetScene().CreateEntity(
            Vec3d{ 32, 0, 0 });
        world.FocusDocument().GetScene().GetRegistry().Components.AddComponent(
            gate, DockGateBinding{ DockId{ 0xdead } });
        world.FocusDocument().MarkDirty();
        // Focus back before saving: the reload restores the recorded focus, and
        // the zone under test has to come back closed.
        ASSERT_TRUE(world.SetFocusZone(first));
        ASSERT_TRUE(world.SaveWorldAs(WorldPath()));
    }

    WorldDocument reloaded(Logging);
    ASSERT_TRUE(reloaded.LoadWorld(WorldPath()));
    if (reloaded.IsZoneOpen(second))
    {
        ASSERT_TRUE(reloaded.UnloadZone(second));
    }
    ASSERT_FALSE(reloaded.IsZoneOpen(second))
        << "the zone under test must be closed, or live validation covers it";

    const WorldCookResult cooked = CookWorld(reloaded, Root, 16.0, Logging, nullptr);
    EXPECT_FALSE(cooked.Success);
    EXPECT_NE(cooked.Error.find("references missing dock"), std::string::npos)
        << cooked.Error;
    EXPECT_NE(cooked.Error.find("Second"), std::string::npos) << cooked.Error;
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
    { return ReadCookedScene(Root / sceneRef).Entities.size(); };

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
    EXPECT_EQ(manifest.WorldSceneRef, "levels/test_world.sscene");
    ASSERT_FALSE(manifest.CookedWorldSceneRef.empty());
    EXPECT_NE(manifest.CookedWorldContentHash, 0u);
    EXPECT_TRUE(fs::exists(Root / manifest.CookedWorldSceneRef));
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
    constexpr std::string_view cookedSuffix = ".smap";
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

TEST_F(WorldCookTest, RefusesDuplicatePersistentIdsAcrossZones)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
    ASSERT_TRUE(world.SetZoneBounds(second,
        Aabb3d::FromMinMax(Vec3d{ 24, 0, -5 }, Vec3d{ 40, 4, 5 })));

    const EntityId original =
        world.FocusDocument().GetScene().CreateEntity(Vec3d{ 0, 0, 0 });
    const PersistentIdComponent* originalId =
        world.FocusDocument().GetRegistry().Components.TryGet<PersistentIdComponent>(
            original);
    ASSERT_NE(originalId, nullptr);
    ASSERT_TRUE(originalId->Id.IsValid());

    // The editor cannot author this state: it models a .level file duplicated
    // by hand, whose entities keep their source ids. Written straight into the
    // registry because EditorScene refuses to mint a collision.
    const PersistentEntityId duplicated = originalId->Id;
    ASSERT_TRUE(world.SetFocusZone(second));
    const EntityId copy =
        world.FocusDocument().GetScene().CreateEntity(Vec3d{ 32, 0, 0 });
    world.FocusDocument().GetScene().GetRegistry().Components
        .TryGet<PersistentIdComponent>(copy)->Id = duplicated;
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    const WorldCookResult cooked = CookWorld(world, Root, 16.0, Logging, nullptr);
    EXPECT_FALSE(cooked.Success);
    EXPECT_NE(cooked.Error.find("duplicate persistent entity id"), std::string::npos)
        << cooked.Error;
    EXPECT_NE(cooked.Error.find("Second"), std::string::npos) << cooked.Error;
}

namespace
{
// Builds a two-zone world, saves it, and returns the second zone's scene ref.
// The second zone is the one under test because the reload restores focus to
// the first, leaving the second closed — and only a closed zone reaches cook's
// own document load.
struct ClosedZoneWorld
{
    ZoneId Closed;
    std::string SceneRef;
};
} // namespace

TEST_F(WorldCookTest, RefusesAZoneWithoutPersistentIdentity)
{
    // A closed zone whose file carries no identity. Cook loads closed zones
    // itself, and that load now refuses malformed content instead of minting
    // ids the source never recorded.
    ClosedZoneWorld setup;
    {
        WorldDocument world(Logging);
        world.NewWorld("TestWorld");
        const ZoneId first = world.Manifest().Zones[0].Id;
        world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
        setup.Closed = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
        ASSERT_TRUE(world.SetZoneBounds(setup.Closed,
            Aabb3d::FromMinMax(Vec3d{ 24, 0, -5 }, Vec3d{ 40, 4, 5 })));
        ASSERT_TRUE(world.SetFocusZone(setup.Closed));
        world.FocusDocument().GetScene().CreateBrush(Vec3d{ 32, 0, 0 });
        ASSERT_TRUE(world.SetFocusZone(first));
        ASSERT_TRUE(world.SaveWorldAs(WorldPath()));
        setup.SceneRef = world.Manifest().Zones[1].SceneRef;
    }
    ASSERT_FALSE(setup.SceneRef.empty());

    // Record the zone as closed while its file is still well-formed; a zone that
    // comes back open would be refused by the editor load instead.
    {
        WorldDocument settle(Logging);
        ASSERT_TRUE(settle.LoadWorld(WorldPath()));
        if (settle.IsZoneOpen(setup.Closed))
        {
            ASSERT_TRUE(settle.UnloadZone(setup.Closed));
        }
        ASSERT_TRUE(settle.SaveWorld());
    }
    EditSavedScene(Root / setup.SceneRef, StripPersistentIds);

    WorldDocument reloaded(Logging);
    ASSERT_TRUE(reloaded.LoadWorld(WorldPath()));
    ASSERT_FALSE(reloaded.IsZoneOpen(setup.Closed))
        << "the zone under test must be closed, or the editor load covers it";

    const WorldCookResult cooked = CookWorld(reloaded, Root, 16.0, Logging, nullptr);
    EXPECT_FALSE(cooked.Success);
    EXPECT_NE(cooked.Error.find("could not load"), std::string::npos) << cooked.Error;
    EXPECT_NE(cooked.Error.find("Second"), std::string::npos) << cooked.Error;
}

TEST_F(WorldCookTest, RefusesReservedRuntimeNamespaceIds)
{
    // Bit 63 belongs to the runtime allocator, so no editor mint produces one
    // and a file carrying one is refused at document load before the cook sees
    // it. The cook gate covers live registry state that never crossed a load,
    // modelled here by writing the id straight into the world scene.
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    EditorDocument& worldScene = world.WorldSceneDocument();
    const EntityId entity = worldScene.GetScene().CreateEntity(Vec3d{});
    worldScene.MarkDirty();
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    worldScene.GetScene().GetRegistry().Components
        .TryGet<PersistentIdComponent>(entity)->Id =
            PersistentEntityId{ PersistentEntityIdRuntimeBit | 0x5678 };

    const WorldCookResult cooked = CookWorld(world, Root, 16.0, Logging, nullptr);
    EXPECT_FALSE(cooked.Success);
    EXPECT_NE(cooked.Error.find("reserved runtime namespace"), std::string::npos)
        << cooked.Error;
}

TEST_F(WorldCookTest, CookedSceneCarriesAuthoredIdsAndGeneratedEntitiesCarryNone)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");

    // One brush (cooks into a generated cell mesh) and one plain authored
    // entity (passes through with its identity).
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    const EntityId authored =
        world.FocusDocument().GetScene().CreateEntity(Vec3d{ 2, 0, 0 });
    const PersistentIdComponent* authoredId =
        world.FocusDocument().GetRegistry().Components.TryGet<PersistentIdComponent>(
            authored);
    ASSERT_NE(authoredId, nullptr);
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    const WorldCookResult cooked = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(cooked.Success) << cooked.Error;

    const WorldPartitionManifest manifest = ParseCookedManifest(cooked.CookedManifestPath);
    ASSERT_FALSE(manifest.Zones.empty());
    const SmapContents scene =
        ReadCookedScene(Root / manifest.Zones[0].CookedSceneRef);

    bool sawAuthoredId = false;
    std::size_t withoutId = 0;
    for (const SmapEntityRecord& record : scene.Entities)
    {
        if (!record.Persistent.IsValid())
        {
            ++withoutId;
            continue;
        }
        sawAuthoredId = sawAuthoredId || record.Persistent == authoredId->Id;
    }
    EXPECT_TRUE(sawAuthoredId)
        << "the authored entity's id must cook through verbatim";
    EXPECT_GT(withoutId, 0u)
        << "cook-generated entities (cell meshes) must not carry identity";
}
