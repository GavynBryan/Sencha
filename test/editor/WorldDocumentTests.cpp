#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/WorldDocument.h"
#include "document/ZoneBounds.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

class WorldDocumentTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_worlddoc_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    [[nodiscard]] std::string WorldPath() const { return (Root / "test.sworld").string(); }

    LoggingProvider Logging;   // sink-less: silent
    fs::path Root;
};

// Resolves an open zone's document through the public iteration surface.
EditorDocument& OpenZoneDocument(WorldDocument& world, ZoneId zone)
{
    EditorDocument* result = nullptr;
    world.VisitOpenZones([&](ZoneId visited, EditorDocument& document, const ZoneViewState&)
                         {
                             if (visited == zone)
                                 result = &document;
                         });
    EXPECT_NE(result, nullptr);
    return *result;
}

} // namespace

TEST_F(WorldDocumentTest, LegacyModeWrapsSingleDocument)
{
    WorldDocument world(Logging);

    EXPECT_FALSE(world.IsWorld());
    EXPECT_FALSE(world.FocusZone().IsValid());
    EXPECT_FALSE(world.IsDirty());
    EXPECT_EQ(world.FocusDocument().GetScene().GetEntityCount(), 0u);
}

TEST_F(WorldDocumentTest, NewWorldHasOneGraphOneZoneFocused)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");

    EXPECT_TRUE(world.IsWorld());
    ASSERT_EQ(world.Manifest().Graphs.size(), 1u);
    ASSERT_EQ(world.Manifest().Zones.size(), 1u);
    EXPECT_TRUE(world.Manifest().Zones[0].Id.IsValid());
    EXPECT_EQ(world.Manifest().Zones[0].Graph, world.Manifest().Graphs[0].Id);
    EXPECT_EQ(world.Manifest().StartZone, world.Manifest().Zones[0].Id);
    EXPECT_EQ(world.FocusZone(), world.Manifest().Zones[0].Id);
    EXPECT_TRUE(world.IsZoneOpen(world.FocusZone()));
    EXPECT_TRUE(world.IsDirty());
}

TEST_F(WorldDocumentTest, MintedIdsAreNonzeroAndUniqueAcrossManifest)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");

    const GraphId graph = world.Manifest().Graphs[0].Id;
    std::vector<uint64_t> minted;
    minted.push_back(graph.Value);
    minted.push_back(world.Manifest().Zones[0].Id.Value);
    for (int i = 0; i < 16; ++i)
        minted.push_back(world.AddZone(graph, "Zone").Value);

    for (size_t a = 0; a < minted.size(); ++a)
    {
        EXPECT_NE(minted[a], 0u);
        for (size_t b = a + 1; b < minted.size(); ++b)
            EXPECT_NE(minted[a], minted[b]);
    }
}

TEST_F(WorldDocumentTest, LoadZoneAssignsUniqueMonotonicRegistryIds)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");

    const GraphId graph = world.Manifest().Graphs[0].Id;
    const ZoneId first = world.Manifest().Zones[0].Id;
    const ZoneId second = world.AddZone(graph, "Second");
    const ZoneId third = world.AddZone(graph, "Third");

    // The world scene document takes index 2 (created before any zone opens).
    EXPECT_EQ(world.WorldSceneDocument().GetRegistry().Id.Index, 2u);
    EXPECT_EQ(world.FocusDocument().GetRegistry().Id.Index, 3u);

    ASSERT_TRUE(world.LoadZone(second));
    EXPECT_EQ(OpenZoneDocument(world, second).GetRegistry().Id.Index, 4u);

    ASSERT_TRUE(world.UnloadZone(second));
    ASSERT_TRUE(world.LoadZone(third));
    EXPECT_EQ(OpenZoneDocument(world, third).GetRegistry().Id.Index, 5u);

    EXPECT_EQ(world.FocusDocument().GetRegistry().Zone, first);
}

TEST_F(WorldDocumentTest, SetFocusLoadsAndFiresObserverOnce)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");

    int fired = 0;
    world.OnFocusChanged = [&fired] { ++fired; };

    ASSERT_TRUE(world.SetFocusZone(second));
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(world.IsZoneOpen(second));
    EXPECT_EQ(world.FocusZone(), second);

    // Focusing the already-focused zone is a no-op and must not fire again.
    ASSERT_TRUE(world.SetFocusZone(second));
    EXPECT_EQ(fired, 1);
}

TEST_F(WorldDocumentTest, UnloadRefusesDirtyZone)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");

    ASSERT_TRUE(world.LoadZone(second));
    OpenZoneDocument(world, second).MarkDirty();

    EXPECT_FALSE(world.UnloadZone(second));
    EXPECT_TRUE(world.IsZoneOpen(second));

    OpenZoneDocument(world, second).MarkDirty(false);
    EXPECT_TRUE(world.UnloadZone(second));
    EXPECT_FALSE(world.IsZoneOpen(second));
}

TEST_F(WorldDocumentTest, WorldSaveLoadRoundTripsManifestAndZoneScenes)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const GraphId graph = world.Manifest().Graphs[0].Id;
    const ZoneId first = world.Manifest().Zones[0].Id;
    const ZoneId second = world.AddZone(graph, "Second");

    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    ASSERT_TRUE(world.LoadZone(second));
    OpenZoneDocument(world, second).GetScene().CreateBrush(Vec3d{ 32, 0, 0 });

    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    WorldDocument reloaded(Logging);
    ASSERT_TRUE(reloaded.LoadWorld(WorldPath()));

    EXPECT_EQ(reloaded.Manifest().Name, "TestWorld");
    ASSERT_EQ(reloaded.Manifest().Zones.size(), 2u);
    EXPECT_EQ(reloaded.Manifest().Zones[0].Id, first);
    EXPECT_EQ(reloaded.Manifest().Zones[1].Id, second);
    EXPECT_EQ(reloaded.Manifest().Graphs[0].Id, graph);
    EXPECT_EQ(reloaded.FocusZone(), first);
    EXPECT_EQ(reloaded.FocusDocument().GetScene().GetEntityCount(), 1u);

    ASSERT_TRUE(reloaded.LoadZone(second));
    EXPECT_EQ(OpenZoneDocument(reloaded, second).GetScene().GetEntityCount(), 1u);
}

TEST_F(WorldDocumentTest, SaveWritesSceneFilesForNewZones)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    world.AddZone(world.Manifest().Graphs[0].Id, "East Hall 2");

    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));

    ASSERT_EQ(world.Manifest().Zones.size(), 2u);
    EXPECT_EQ(world.Manifest().Zones[0].SceneRef, "levels/zone_1.level.json");
    EXPECT_EQ(world.Manifest().Zones[1].SceneRef, "levels/east_hall_2.level.json");
    EXPECT_TRUE(fs::exists(Root / "levels/zone_1.level.json"));
    EXPECT_TRUE(fs::exists(Root / "levels/east_hall_2.level.json"));
}

TEST_F(WorldDocumentTest, VisitOpenZonesFollowsManifestOrder)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const GraphId graph = world.Manifest().Graphs[0].Id;
    const ZoneId first = world.Manifest().Zones[0].Id;
    const ZoneId second = world.AddZone(graph, "Second");
    const ZoneId third = world.AddZone(graph, "Third");

    // Load out of manifest order: iteration must still follow the manifest.
    ASSERT_TRUE(world.LoadZone(third));
    ASSERT_TRUE(world.LoadZone(second));

    std::vector<ZoneId> visited;
    world.VisitOpenZones([&](ZoneId zone, EditorDocument&, const ZoneViewState&)
                         { visited.push_back(zone); });

    ASSERT_EQ(visited.size(), 3u);
    EXPECT_EQ(visited[0], first);
    EXPECT_EQ(visited[1], second);
    EXPECT_EQ(visited[2], third);
}

TEST_F(WorldDocumentTest, UserSidecarRoundTripsFocusAndZoneStates)
{
    const ZoneId first = [&]
    {
        WorldDocument world(Logging);
        world.NewWorld("TestWorld");
        const ZoneId zoneOne = world.Manifest().Zones[0].Id;
        const ZoneId zoneTwo = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
        EXPECT_TRUE(world.SaveWorldAs(WorldPath()));
        EXPECT_TRUE(world.SetFocusZone(zoneTwo));
        EXPECT_TRUE(world.SetZoneVisible(zoneOne, false));
        EXPECT_TRUE(world.SaveWorld());
        return zoneOne;
    }();

    WorldDocument reloaded(Logging);
    ASSERT_TRUE(reloaded.LoadWorld(WorldPath()));

    EXPECT_EQ(reloaded.FocusZone(), reloaded.Manifest().Zones[1].Id);
    EXPECT_TRUE(reloaded.IsZoneOpen(first));

    bool firstVisible = true;
    reloaded.VisitOpenZones([&](ZoneId zone, EditorDocument&, const ZoneViewState& view)
                            {
                                if (zone == first)
                                    firstVisible = view.VisibleInEditor;
                            });
    EXPECT_FALSE(firstVisible);
}

TEST_F(WorldDocumentTest, MissingSidecarYieldsDefaults)
{
    ZoneId second{};
    {
        WorldDocument world(Logging);
        world.NewWorld("TestWorld");
        second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
        ASSERT_TRUE(world.SaveWorldAs(WorldPath()));
        ASSERT_TRUE(world.SetFocusZone(second));
        ASSERT_TRUE(world.SaveWorld());
    }
    fs::remove(WorldPath() + ".user.json");

    WorldDocument reloaded(Logging);
    ASSERT_TRUE(reloaded.LoadWorld(WorldPath()));

    // Defaults: the start zone focused, nothing else open.
    EXPECT_EQ(reloaded.FocusZone(), reloaded.Manifest().StartZone);
    EXPECT_FALSE(reloaded.IsZoneOpen(second));
}

TEST_F(WorldDocumentTest, SceneUnresolvedRecordFiresForMissingFile)
{
    ZoneId second{};
    {
        WorldDocument world(Logging);
        world.NewWorld("TestWorld");
        second = world.AddZone(world.Manifest().Graphs[0].Id, "Second");
        ASSERT_TRUE(world.SaveWorldAs(WorldPath()));
    }
    fs::remove(Root / "levels/second.level.json");

    WorldDocument reloaded(Logging);
    ASSERT_TRUE(reloaded.LoadWorld(WorldPath()));

    bool fired = false;
    for (const ContentRiskRecord& record : reloaded.ValidationRecords())
    {
        if (record.RuleId != "partition.zone.scene_unresolved")
            continue;
        fired = true;
        EXPECT_EQ(record.Severity, ContentRiskSeverity::Error);
        EXPECT_EQ(record.Kind, ContentRiskSourceKind::Zone);
        EXPECT_EQ(record.SourceId, second.Value);
    }
    EXPECT_TRUE(fired);
}

TEST_F(WorldDocumentTest, ComputeZoneBoundsUnionsEntities)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    EditorScene& scene = world.FocusDocument().GetScene();
    scene.CreateBrush(Vec3d{ 0, 0, 0 }, Vec3d{ 1, 1, 1 });
    scene.CreateBrush(Vec3d{ 10, 0, 0 }, Vec3d{ 2, 2, 2 });

    const auto bounds = ComputeZoneBounds(scene);

    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(bounds->Min, (Vec3d{ -1.0f, -2.0f, -2.0f }));
    EXPECT_EQ(bounds->Max, (Vec3d{ 12.0f, 2.0f, 2.0f }));

    EditorDocument empty(Logging);
    EXPECT_FALSE(ComputeZoneBounds(empty.GetScene()).has_value());
}

TEST_F(WorldDocumentTest, SaveDerivesBoundsUntilExplicitOverride)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId zone = world.Manifest().Zones[0].Id;
    world.FocusDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 }, Vec3d{ 4, 4, 4 });

    ASSERT_TRUE(world.SaveWorldAs(WorldPath()));
    EXPECT_EQ(world.Manifest().Zones[0].Bounds.Max, (Vec3d{ 4.0f, 4.0f, 4.0f }));

    const Aabb3d authored{ { -100.0f, 0.0f, -100.0f }, { 100.0f, 10.0f, 100.0f } };
    ASSERT_TRUE(world.SetZoneBounds(zone, authored));
    ASSERT_TRUE(world.SaveWorld());

    EXPECT_EQ(world.Manifest().Zones[0].Bounds, authored);
    EXPECT_TRUE(world.Manifest().Zones[0].BoundsOverridden);
    EXPECT_EQ(world.Manifest().Zones[0].Id, zone);
}

TEST_F(WorldDocumentTest, SaveWorldAsEnforcesSworldExtension)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");

    // A dialog-produced .json name still lands as .sworld.
    ASSERT_TRUE(world.SaveWorldAs((Root / "mistyped.json").string()));
    EXPECT_TRUE(std::string(world.WorldPath()).ends_with(".sworld"));
    EXPECT_TRUE(fs::exists(Root / "mistyped.sworld"));
    EXPECT_FALSE(fs::exists(Root / "mistyped.json"));

    // No extension at all gets one too.
    ASSERT_TRUE(world.SaveWorldAs((Root / "bare").string()));
    EXPECT_TRUE(fs::exists(Root / "bare.sworld"));
}

TEST_F(WorldDocumentTest, WorldSceneRoundTripsThroughSaveAndLoad)
{
    {
        WorldDocument world(Logging);
        world.NewWorld("TestWorld");
        world.WorldSceneDocument().GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
        ASSERT_TRUE(world.SaveWorldAs(WorldPath()));
        EXPECT_EQ(world.Manifest().WorldSceneRef, "levels/test_world.level.json");
        EXPECT_TRUE(fs::exists(Root / "levels/test_world.level.json"));
    }

    WorldDocument reloaded(Logging);
    ASSERT_TRUE(reloaded.LoadWorld(WorldPath()));

    EXPECT_EQ(reloaded.Manifest().WorldSceneRef, "levels/test_world.level.json");
    EXPECT_EQ(reloaded.WorldSceneDocument().GetScene().GetEntityCount(), 1u);

    // The world scene is not a zone: open-zone iteration never visits it.
    size_t visited = 0;
    reloaded.VisitOpenZones([&](ZoneId, EditorDocument&, const ZoneViewState&) { ++visited; });
    EXPECT_EQ(visited, 1u);

    // Its dirt counts as the world's dirt, so save prompts cover it.
    EXPECT_FALSE(reloaded.IsDirty());
    reloaded.WorldSceneDocument().MarkDirty();
    EXPECT_TRUE(reloaded.IsDirty());
}

TEST_F(WorldDocumentTest, FocusWorldSceneSwitchesFocusDocumentAndFiresOnce)
{
    WorldDocument world(Logging);
    world.NewWorld("TestWorld");
    const ZoneId zone = world.Manifest().Zones[0].Id;

    int fired = 0;
    world.OnFocusChanged = [&fired] { ++fired; };

    ASSERT_TRUE(world.FocusWorldScene());
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(world.IsWorldSceneFocused());
    EXPECT_FALSE(world.FocusZone().IsValid());
    EXPECT_EQ(&world.FocusDocument(), &world.WorldSceneDocument());

    // While the world scene is focused every zone is context: the previous
    // focus zone may unload.
    ASSERT_TRUE(world.UnloadZone(zone));

    // Focusing the already-focused world scene is a no-op.
    ASSERT_TRUE(world.FocusWorldScene());
    EXPECT_EQ(fired, 1);

    // A zone focus leaves the world scene (reloading the zone on the way).
    ASSERT_TRUE(world.SetFocusZone(zone));
    EXPECT_EQ(fired, 2);
    EXPECT_FALSE(world.IsWorldSceneFocused());
    EXPECT_EQ(world.FocusZone(), zone);
}

TEST_F(WorldDocumentTest, SidecarRecordsWorldSceneFocus)
{
    {
        WorldDocument world(Logging);
        world.NewWorld("TestWorld");
        ASSERT_TRUE(world.SaveWorldAs(WorldPath()));
        ASSERT_TRUE(world.FocusWorldScene());
        ASSERT_TRUE(world.SaveWorld());
    }

    WorldDocument reloaded(Logging);
    ASSERT_TRUE(reloaded.LoadWorld(WorldPath()));

    EXPECT_TRUE(reloaded.IsWorldSceneFocused());
    EXPECT_FALSE(reloaded.FocusZone().IsValid());
    EXPECT_EQ(&reloaded.FocusDocument(), &reloaded.WorldSceneDocument());
}

TEST_F(WorldDocumentTest, AbsentWorldSceneKeyLoadsAsEmptyWorldScene)
{
    {
        WorldDocument world(Logging);
        world.NewWorld("TestWorld");
        ASSERT_TRUE(world.SaveWorldAs(WorldPath()));
    }

    // A pre-world-scene manifest carries no "world_scene" key: strip it from
    // the saved file to reproduce a legacy world.
    {
        std::ifstream in(WorldPath(), std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        auto json = JsonParse(buffer.str());
        ASSERT_TRUE(json.has_value());
        std::erase_if(json->AsObject(),
                      [](const auto& field) { return field.first == "world_scene"; });
        std::ofstream out(WorldPath(), std::ios::binary | std::ios::trunc);
        out << JsonStringify(*json, /*pretty*/ true);
    }

    WorldDocument reloaded(Logging);
    ASSERT_TRUE(reloaded.LoadWorld(WorldPath()));

    EXPECT_TRUE(reloaded.IsWorld());
    EXPECT_TRUE(reloaded.Manifest().WorldSceneRef.empty());
    EXPECT_EQ(reloaded.WorldSceneDocument().GetScene().GetEntityCount(), 0u);
    EXPECT_FALSE(reloaded.IsWorldSceneFocused());
}

TEST_F(WorldDocumentTest, LoadRoutesWorldManifestByContent)
{
    ZoneId zone{};
    {
        WorldDocument world(Logging);
        world.NewWorld("TestWorld");
        zone = world.Manifest().Zones[0].Id;
        ASSERT_TRUE(world.SaveWorldAs(WorldPath()));
    }
    // A world manifest that ended up under the level extension (the historical
    // Save As bug) still opens as a world through the legacy Load entry.
    const fs::path disguised = Root / "disguised.json";
    fs::copy_file(WorldPath(), disguised);

    WorldDocument reopened(Logging);
    ASSERT_TRUE(reopened.Load(disguised.string()));
    EXPECT_TRUE(reopened.IsWorld());
    ASSERT_EQ(reopened.Manifest().Zones.size(), 1u);
    EXPECT_EQ(reopened.Manifest().Zones[0].Id, zone);

    // A plain level file keeps routing to the legacy document.
    EditorDocument level(Logging);
    level.GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    const fs::path levelPath = Root / "plain.level.json";
    ASSERT_TRUE(level.SaveAs(levelPath.string()));
    ASSERT_TRUE(reopened.Load(levelPath.string()));
    EXPECT_FALSE(reopened.IsWorld());
    EXPECT_EQ(reopened.FocusDocument().GetScene().GetEntityCount(), 1u);
}
