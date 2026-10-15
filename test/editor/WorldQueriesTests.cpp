// world.summary and diagnostics: the world's zones, the docks between them,
// and what its validation reports, as data.

#include "document/DocumentSerialization.h"
#include "document/WorldDocument.h"
#include "operations/EditorOperations.h"
#include "operations/WorldQueries.h"

#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    class WorldQueriesTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        JsonValue Ask(EditorOperations& operations, std::string_view name)
        {
            return operations.Invoke(name, JsonValue(JsonValue::Object{}));
        }

        LoggingProvider Logging;
    };

    TEST_F(WorldQueriesTest, TheSummaryListsZonesAndTheDocksBetweenThem)
    {
        // The template world, read from the repository; ignoring the user state
        // means this test can never leave a sidecar there.
        WorldDocument world(Logging, WorldDocument::UserState::Ignored);
        ASSERT_TRUE(world.LoadWorld(std::string(SENCHA_REPO_ROOT) + "/templates/fps/assets/traversal3.sworld"));
        EditorOperations operations;
        std::vector<std::string> errors;
        ASSERT_TRUE(operations.Register("world", WorldQueryOperations(world), errors));

        const JsonValue answer = Ask(operations, "world.summary");
        ASSERT_EQ(answer.Find("status")->AsString(), "ok") << JsonStringify(answer);
        const JsonValue& data = *answer.Find("data");
        ASSERT_EQ(data.Find("zones")->AsArray().size(), 3u);
        const JsonValue::Array& docks = data.Find("docks")->AsArray();
        ASSERT_EQ(docks.size(), 2u);

        const JsonValue& first = docks.front();
        EXPECT_EQ(first.Find("zone_a_name")->AsString(), "Zone 1");
        EXPECT_EQ(first.Find("zone_b_name")->AsString(), "Room 2");
        EXPECT_DOUBLE_EQ(first.Find("position")->AsArray()[0].AsNumber(), 8.0);
        // Turned a quarter about Y, facing along X: zone_a lies on the -X side.
        EXPECT_NEAR(std::abs(first.Find("normal")->AsArray()[0].AsNumber()), 1.0, 1e-3);
        EXPECT_EQ(first.Find("directions")->AsString(), "both");
    }

    TEST_F(WorldQueriesTest, DiagnosticsReportWhatValidationFound)
    {
        const fs::path root = fs::temp_directory_path()
            / ("sencha_world_queries_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(root);
        fs::create_directories(root);
        const std::string path = (root / "unreachable.sworld").string();
        {
            WorldDocument authored(Logging, WorldDocument::UserState::Ignored);
            authored.NewWorld("Unreachable");
            (void)authored.AddZone(authored.Manifest().Graphs[0].Id, "Island");
            ASSERT_TRUE(authored.SaveWorldAs(path));
        }

        WorldDocument world(Logging, WorldDocument::UserState::Ignored);
        ASSERT_TRUE(world.LoadWorld(path));
        EditorOperations operations;
        std::vector<std::string> errors;
        ASSERT_TRUE(operations.Register("world", WorldQueryOperations(world), errors));

        const JsonValue answer = Ask(operations, "diagnostics");
        ASSERT_EQ(answer.Find("status")->AsString(), "ok") << JsonStringify(answer);
        bool unreachable = false;
        for (const JsonValue& record : answer.Find("data")->Find("records")->AsArray())
            unreachable = unreachable || record.Find("rule")->AsString() == "partition.graph.unreachable";
        EXPECT_TRUE(unreachable) << JsonStringify(answer);

        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TEST_F(WorldQueriesTest, ASingleLevelHasNoWorldSummary)
    {
        WorldDocument level(Logging);
        EditorOperations operations;
        std::vector<std::string> errors;
        ASSERT_TRUE(operations.Register("world", WorldQueryOperations(level), errors));
        EXPECT_EQ(Ask(operations, "world.summary").Find("code")->AsString(), "world.none");
        EXPECT_TRUE(Ask(operations, "diagnostics").Find("data")->Find("records")->AsArray().empty());
    }
}
