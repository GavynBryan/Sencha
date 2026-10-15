// world.crossings and dock.place_at on the template world: walkable surface
// that runs from one zone into the next past the edges of its dock, and a dock
// fitted to a reported crossing or to an opening, proposed in a draft of the
// world scene.

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"
#include "operations/WorldQueries.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <world/registry/Registry.h>
#include <zone/WorldConnectionComponents.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    class WorldCrossingsTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            // Read from the repository; ignoring the user state means nothing is written there.
            World.SetContentRoots({ std::string(SENCHA_REPO_ROOT) + "/templates/fps/assets" });
            ASSERT_TRUE(World.LoadWorld(std::string(SENCHA_REPO_ROOT) + "/templates/fps/assets/traversal3.sworld"));
            std::vector<std::string> errors;
            ASSERT_TRUE(Operations.Register("level", LevelOperations(Level, Operations), errors));
            ASSERT_TRUE(Operations.Register("world", WorldOperations(World), errors));
        }

        JsonValue Call(std::string_view name, std::string_view arguments)
        {
            return Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
        }

        JsonValue Ok(std::string_view name, std::string_view arguments)
        {
            const JsonValue answer = Call(name, arguments);
            EXPECT_EQ(answer.Find("status")->AsString(), "ok") << name << ": " << JsonStringify(answer);
            const JsonValue* data = answer.Find("data");
            return data != nullptr ? *data : JsonValue{};
        }

        static std::string Code(const JsonValue& answer)
        {
            const JsonValue* code = answer.Find("code");
            return code != nullptr ? code->AsString() : std::string{};
        }

        JsonValue::Array Crossings(std::string_view zone)
        {
            return Ok("world.crossings", R"({"zone": ")" + std::string(zone) + R"("})").Find("crossings")->AsArray();
        }

        std::size_t Docks()
        {
            std::size_t count = 0;
            const EditorScene& scene = World.WorldSceneDocument().GetScene();
            for (const EntityId entity : scene.GetAllEntities())
                count += scene.GetRegistry().Components.HasComponent<WorldDock>(entity) ? 1 : 0;
            return count;
        }

        LoggingProvider Logging;
        WorldDocument World{ Logging, WorldDocument::UserState::Ignored };
        CommandStack Commands;
        LevelTarget Level{ World, Commands };
        EditorOperations Operations;
    };

    TEST_F(WorldCrossingsTest, TheFloorRunsOnPastBothEndsOfTheDock)
    {
        const JsonValue::Array crossings = Crossings("Zone 1");
        ASSERT_EQ(crossings.size(), 2u) << "the 6 m dock leaves 16 m of shared floor uncovered at both ends";
        for (const JsonValue& crossing : crossings)
        {
            EXPECT_EQ(crossing.Find("code")->AsString(), "spatial.crossing_without_dock");
            EXPECT_EQ(crossing.Find("beyond")->Find("name")->AsString(), "Room 2");
            EXPECT_EQ(crossing.Find("facing")->AsString(), "east");
            EXPECT_NEAR(crossing.Find("at")->AsArray()[0].AsNumber(), 8.0, 1e-3);
            EXPECT_GT(crossing.Find("width")->AsNumber(), 4.0);
        }
        EXPECT_TRUE(crossings[0].Find("confirmed")->IsNull()) << "Room 2 is closed, so its side is not known";

        ASSERT_TRUE(World.LoadZone(World.Manifest().Zones[1].Id));
        EXPECT_TRUE(Crossings("Zone 1")[0].Find("confirmed")->AsBool()) << "Room 2's floor meets it";
    }

    TEST_F(WorldCrossingsTest, ADockProposedForACrossingCoversItOnceAccepted)
    {
        const JsonValue crossing = Crossings("Zone 1").front();
        EXPECT_EQ(Code(Call("dock.place_at", R"({"at": [8, 0, 0], "width": 2, "facing": "east"})")),
                  "dock.needs_world_scene");

        Ok("world.focus", R"({"target": "world"})");
        Ok("draft.begin", R"({"label": "Dock the floor"})");
        Ok("dock.place_at", R"({"at": )" + JsonStringify(*crossing.Find("at")) + R"(, "width": )"
                                + JsonStringify(*crossing.Find("width")) + R"(, "facing": "east"})");
        EXPECT_EQ(Docks(), 2u) << "only proposed";
        Ok("draft.accept", "{}");
        EXPECT_EQ(Docks(), 3u);
        EXPECT_EQ(Crossings("Zone 1").size(), 1u);

        Commands.Undo();
        EXPECT_EQ(Crossings("Zone 1").size(), 2u);
    }

    TEST_F(WorldCrossingsTest, ADockFitsAnOpeningInTheBoundaryWall)
    {
        Ok("world.focus", R"({"target": "Zone 1"})");
        Ok("space.create", R"({"name": "Lobby", "min": [2, 0, -2], "max": [7.75, 3, 2]})");
        Ok("opening.create", R"({"at": [7.875, 1, 0], "name": "Gate", "width": 1.5, "height": 2.5})");

        Ok("world.focus", R"({"target": "world"})");
        const JsonValue placed = Ok("dock.place_at", R"({"opening": "@Gate"})");
        const SubjectResolution dock = ResolveEntitySubject(World.WorldSceneDocument(),
                                                            placed.Find("dock")->Find("id")->AsString());
        ASSERT_TRUE(dock.Resolved());
        const WorldDock& made = *World.WorldSceneDocument().GetScene().GetRegistry().Components.TryGet<WorldDock>(
            dock.Entities.front());
        EXPECT_NEAR(made.HalfExtents.X, 0.75f, 1e-4f);
        EXPECT_NEAR(made.HalfExtents.Y, 1.25f, 1e-4f);
        EXPECT_EQ(made.ZoneA, World.Manifest().Zones[0].Id) << "it leaves the zone the opening stands in";
        EXPECT_EQ(made.ZoneB, World.Manifest().Zones[1].Id);
    }

    TEST_F(WorldCrossingsTest, FocusTakesAZoneByNameOrTheWorldScene)
    {
        EXPECT_EQ(Ok("world.focus", R"({"target": "Room 3"})").Find("focus")->AsString(),
                  ZoneIdToString(World.Manifest().Zones[2].Id));
        EXPECT_EQ(Code(Call("world.focus", R"({"target": "Nowhere"})")), "world.zone_unknown");
        Ok("world.focus", R"({"target": "world"})");
        EXPECT_TRUE(World.IsWorldSceneFocused());
    }
}
