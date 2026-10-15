// measure.clearance: floor, ceiling and width around a point, measured against
// the level's brushes, and whether the navigation profile's capsule fits.

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "operations/EditorOperations.h"
#include "operations/LevelQueries.h"
#include "operations/LevelTarget.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>

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

    class LevelClearanceTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_clearance_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            fs::create_directories(Root / "data");
            std::ofstream(Root / "data/navigation.sdata", std::ios::trunc) << kSettings;
            World.SetContentRoots({ Root });
            std::vector<std::string> errors;
            ASSERT_TRUE(Operations.Register("level", LevelQueryOperations(Level), errors));
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }

        // A corridor running north-south: walls 1 m apart (inner faces at x = -0.5
        // and x = 0.5), floor top at y = 0, ceiling underside at y = `ceiling`.
        void Corridor(float ceiling)
        {
            EditorScene& scene = World.FocusDocument().GetScene();
            (void)scene.CreateBrush(Vec3d{ 0, -0.25f, 0 }, Vec3d{ 2, 0.25f, 6 });
            (void)scene.CreateBrush(Vec3d{ 0, ceiling + 0.25f, 0 }, Vec3d{ 2, 0.25f, 6 });
            (void)scene.CreateBrush(Vec3d{ -0.75f, ceiling * 0.5f, 0 }, Vec3d{ 0.25f, ceiling * 0.5f, 6 });
            (void)scene.CreateBrush(Vec3d{ 0.75f, ceiling * 0.5f, 0 }, Vec3d{ 0.25f, ceiling * 0.5f, 6 });
        }

        JsonValue Measure(std::string_view arguments)
        {
            return Operations.Invoke("measure.clearance", JsonParse(arguments).value_or(JsonValue{}));
        }

        static double Number(const JsonValue& data, std::string_view key)
        {
            return data.Find(key)->AsNumber();
        }

        fs::path Root;
        LoggingProvider Logging;
        WorldDocument World{ Logging };
        CommandStack Commands;
        LevelTarget Level{ World, Commands };
        EditorOperations Operations;
    };

    TEST_F(LevelClearanceTest, MeasuresTheCorridorAndSaysTheCapsuleFits)
    {
        Corridor(2.5f);
        const JsonValue answer = Measure(R"({"point": [0, 1, 0]})");
        ASSERT_EQ(answer.Find("status")->AsString(), "ok") << JsonStringify(answer);
        const JsonValue& data = *answer.Find("data");
        EXPECT_NEAR(Number(data, "floor"), 0.0, 1e-3);
        EXPECT_NEAR(Number(data, "ceiling"), 2.5, 1e-3);
        EXPECT_NEAR(Number(data, "height"), 2.5, 1e-3);
        EXPECT_NEAR(Number(data, "width"), 1.0, 1e-3);
        EXPECT_TRUE(data.Find("profile")->Find("fits_width")->AsBool());
        EXPECT_TRUE(data.Find("profile")->Find("fits_height")->AsBool());
    }

    TEST_F(LevelClearanceTest, ALowCeilingIsTooLowForTheProfile)
    {
        Corridor(1.5f);
        const JsonValue answer = Measure(R"({"point": [0, 0.5, 0]})");
        const JsonValue& data = *answer.Find("data");
        EXPECT_NEAR(Number(data, "height"), 1.5, 1e-3);
        EXPECT_FALSE(data.Find("profile")->Find("fits_height")->AsBool());
    }

    TEST_F(LevelClearanceTest, WidthIsTakenAcrossTheDirectionOfTravel)
    {
        Corridor(2.5f);
        // Travelling east, "across" is north-south, where the corridor runs open.
        const JsonValue answer = Measure(R"({"point": [0, 1, 0], "direction": [1, 0, 0]})");
        const JsonValue& data = *answer.Find("data");
        EXPECT_TRUE(data.Find("width")->IsNull());
    }

    TEST_F(LevelClearanceTest, WithoutNavigationSettingsOnlyTheMeasurementsComeBack)
    {
        fs::remove(Root / "data/navigation.sdata");
        Corridor(2.5f);
        const JsonValue answer = Measure(R"({"point": [0, 1, 0]})");
        const JsonValue& data = *answer.Find("data");
        EXPECT_NEAR(Number(data, "width"), 1.0, 1e-3);
        EXPECT_TRUE(data.Find("profile")->IsNull());
    }
}
