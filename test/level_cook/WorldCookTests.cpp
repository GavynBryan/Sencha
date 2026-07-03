#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/WorldCook.h"
#include "document/WorldDocument.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"

#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <zone/WorldPartitionManifest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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
        second = world.AddZone(world.Manifest().Regions[0].Id, "Second");
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

TEST_F(WorldCookTest, RecookWithoutEditsIsByteIdenticalAndEditsChangeOneHash)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId second = world.AddZone(world.Manifest().Regions[0].Id, "Second");
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
    EXPECT_EQ(ReadFile(Root / edited.Zones[0].CookedSceneRef), artifactsBefore[0]);
}

TEST_F(WorldCookTest, CookReflectsCrossZoneMove)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId second = world.AddZone(world.Manifest().Regions[0].Id, "Second");
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

TEST_F(WorldCookTest, CookStripsPortalBrushes)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    EditorScene& scene = world.FocusDocument().GetScene();
    scene.CreateBrush(Vec3d{ 0, 0, 0 });
    const EntityId portal = scene.CreateBrush(Vec3d{ 8, 0, 0 });
    scene.GetRegistry().Components.AddComponent(
        portal, PortalComponent{ TransitionId{ 0x00000000000000c1ull } });
    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    const WorldCookResult withPortal = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(withPortal.Success) << withPortal.Error;
    const WorldPartitionManifest manifest = ParseCookedManifest(withPortal.CookedManifestPath);
    const std::string cookedScene = ReadFile(Root / manifest.Zones[0].CookedSceneRef);
    const std::string cookedCollision = ReadFile(Root / manifest.Zones[0].CookedCollisionRef);

    // No portal entity and no portal key reach the cooked scene.
    EXPECT_EQ(cookedScene.find("portal"), std::string::npos);

    // Deleting the portal and re-cooking produces byte-identical artifacts:
    // the portal contributed no geometry, collision, or passthrough entity.
    scene.DestroyEntity(portal);
    world.FocusDocument().MarkDirty();
    ASSERT_TRUE(world.SaveWorld());
    const WorldCookResult withoutPortal = CookWorld(world, Root, 16.0, Logging, nullptr);
    ASSERT_TRUE(withoutPortal.Success) << withoutPortal.Error;
    const WorldPartitionManifest after = ParseCookedManifest(withoutPortal.CookedManifestPath);

    EXPECT_EQ(ReadFile(Root / after.Zones[0].CookedSceneRef), cookedScene);
    EXPECT_EQ(ReadFile(Root / after.Zones[0].CookedCollisionRef), cookedCollision);
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
