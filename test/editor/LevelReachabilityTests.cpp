// nav.reachable: the level's walkable surface asked twice, unknown traversal
// kinds blocked and then open, so a verified answer is never confused with one
// that rests on an assumption.

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "operations/EditorOperations.h"
#include "operations/LevelQueries.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <navigation/NavLinkComponent.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    constexpr std::string_view kSettings = R"({
        "type": "navigation.settings", "version": 1,
        "data": {
            "profiles": [ { "tag": "navigation.profile.humanoid", "radius": 0.3,
                            "height": 1.8, "max_slope_degrees": 45, "max_climb": 0.35,
                            "cell_size": 0.2, "cell_height": 0.1, "tile_cells": 40 } ],
            "areas": [] } })";

    class LevelReachabilityTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_reachability_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            fs::create_directories(Root / "data");
            std::ofstream(Root / "data/navigation.sdata", std::ios::trunc) << kSettings;
            World.SetContentRoots({ Root });

            std::vector<std::string> errors;
            ASSERT_TRUE(Operations.Register("level", LevelQueryOperations(World), errors));

            // Two floor slabs, their walkable tops at y = 0, with a 4 m gap.
            EditorScene& scene = World.FocusDocument().GetScene();
            (void)scene.CreateBrush(Vec3d{ 5, -0.5f, 5 }, Vec3d{ 5, 0.5f, 5 });
            (void)scene.CreateBrush(Vec3d{ 19, -0.5f, 5 }, Vec3d{ 5, 0.5f, 5 });
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }

        void JumpAcrossTheGap()
        {
            EditorScene& scene = World.FocusDocument().GetScene();
            const Vec3d entry(9.0f, 0.0f, 5.0f);
            const EntityId entity = scene.CreateEntity(entry);
            NavLink link;
            link.Id = NavLinkId{ 0xabcdef };
            link.Traversal = NavTraversalName("navigation.traversal.jump");
            link.ExitOffset = Vec3d(15.0f, 0.0f, 5.0f) - entry;
            scene.GetRegistry().Components.AddComponent(entity, link);
        }

        JsonValue Ask(std::string_view extra = {})
        {
            const std::string arguments = R"({"from": [2, 0, 5], "to": [20, 0, 5])"
                + std::string(extra) + "}";
            return Operations.Invoke("nav.reachable", JsonParse(arguments).value_or(JsonValue{}));
        }

        static std::string Text(const JsonValue& value, std::string_view key)
        {
            const JsonValue* found = value.Find(key);
            return found != nullptr && found->IsString() ? found->AsString() : std::string{};
        }

        static std::string Outcome(const JsonValue& answer)
        {
            const JsonValue* data = answer.Find("data");
            EXPECT_NE(data, nullptr) << JsonStringify(answer);
            return data != nullptr ? Text(*data, "outcome") : std::string{};
        }

        fs::path Root;
        LoggingProvider Logging;
        WorldDocument World{ Logging };
        EditorOperations Operations;
    };

    TEST_F(LevelReachabilityTest, OneFloorIsReachedWithoutAnyAssumption)
    {
        const JsonValue answer = Operations.Invoke(
            "nav.reachable", JsonParse(R"({"from": [2, 0, 2], "to": [8, 0, 8]})").value_or(JsonValue{}));
        EXPECT_EQ(Outcome(answer), "reached");
        const JsonValue& data = *answer.Find("data");
        EXPECT_EQ(Text(data, "sure"), "success");
        EXPECT_GT(data.Find("route")->Find("length")->AsNumber(), 8.0);
    }

    TEST_F(LevelReachabilityTest, AGapNothingCrossesIsUnreachedWhateverIsAssumed)
    {
        const JsonValue answer = Ask();
        EXPECT_EQ(Outcome(answer), "unreached");
        EXPECT_TRUE(answer.Find("data")->Find("route")->IsNull());
    }

    TEST_F(LevelReachabilityTest, AJumpNobodyMentionedIsWhatTheAnswerDependsOn)
    {
        JumpAcrossTheGap();

        const JsonValue unknown = Ask();
        EXPECT_EQ(Outcome(unknown), "depends_on");
        const JsonValue& data = *unknown.Find("data");
        ASSERT_EQ(data.Find("depends_on")->AsArray().size(), 1u);
        EXPECT_EQ(data.Find("depends_on")->AsArray().front().AsString(), "navigation.traversal.jump");
        EXPECT_EQ(data.Find("assumptions")->Find("unknown")->AsArray().size(), 1u);
        bool crossesTheLink = false;
        for (const JsonValue& step : data.Find("route")->Find("steps")->AsArray())
            crossesTheLink = crossesTheLink || Text(step, "kind") == "link";
        EXPECT_TRUE(crossesTheLink);

        EXPECT_EQ(Outcome(Ask(R"(, "held": ["navigation.traversal.jump"])")), "reached");
        EXPECT_EQ(Outcome(Ask(R"(, "denied": ["navigation.traversal.jump"])")), "unreached");
        // A parent covers its descendants, as the runtime matches capabilities.
        EXPECT_EQ(Outcome(Ask(R"(, "held": ["navigation.traversal"])")), "reached");
    }

    TEST_F(LevelReachabilityTest, ContradictionsAndOffMeshPointsAreRefusedWithCodes)
    {
        JumpAcrossTheGap();
        const JsonValue contradictory =
            Ask(R"(, "held": ["navigation.traversal.jump"], "denied": ["navigation.traversal"])");
        EXPECT_EQ(Text(contradictory, "code"), "scenario.contradictory");

        const JsonValue offMesh = Operations.Invoke(
            "nav.reachable", JsonParse(R"({"from": [2, 0, 5], "to": [100, 0, 5]})").value_or(JsonValue{}));
        EXPECT_EQ(Text(offMesh, "code"), "nav.off_mesh");
        EXPECT_EQ(Text(*offMesh.Find("detail"), "endpoint"), "to");
    }

    TEST_F(LevelReachabilityTest, WithoutNavigationSettingsTheQuestionIsRefused)
    {
        fs::remove(Root / "data/navigation.sdata");
        EXPECT_EQ(Text(Ask(), "code"), "nav.unavailable");
    }
}
