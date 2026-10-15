// space.graph: which spaces the level's walkable surface joins, measured on
// its navigation, a door being what makes two rooms one walk, and a link of an
// unknown kind what makes a connection depend on an assumption.

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelTarget.h"

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

    class SpaceGraphTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_space_graph_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            fs::create_directories(Root / "data");
            std::ofstream(Root / "data/navigation.sdata", std::ios::trunc) << kSettings;
            World.SetContentRoots({ Root });
            std::vector<std::string> errors;
            ASSERT_TRUE(Operations.Register("level", LevelOperations(Level, Operations), errors));
            Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
            Ok("space.create", R"({"name": "Yard", "min": [8.25, 0, 0], "max": [16, 3, 6]})");
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }

        JsonValue Ok(std::string_view name, std::string_view arguments)
        {
            const JsonValue answer = Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
            EXPECT_EQ(answer.Find("status")->AsString(), "ok") << name << ": " << JsonStringify(answer);
            const JsonValue* data = answer.Find("data");
            return data != nullptr ? *data : JsonValue{};
        }

        // "from>to:outcome" for every connection.
        static std::vector<std::string> Connections(const JsonValue& graph)
        {
            std::vector<std::string> out;
            for (const JsonValue& connection : graph.Find("connections")->AsArray())
                out.push_back(connection.Find("from")->Find("name")->AsString() + ">"
                              + connection.Find("to")->Find("name")->AsString() + ":"
                              + connection.Find("outcome")->AsString());
            return out;
        }

        fs::path Root;
        LoggingProvider Logging;
        WorldDocument World{ Logging };
        CommandStack Commands;
        LevelTarget Level{ World, Commands };
        EditorOperations Operations;
    };

    TEST_F(SpaceGraphTest, ADoorInTheSharedWallJoinsTheRooms)
    {
        JsonValue graph = Ok("space.graph", "{}");
        EXPECT_TRUE(Connections(graph).empty()) << "the shared wall stands between them";
        for (const JsonValue& space : graph.Find("spaces")->AsArray())
            EXPECT_TRUE(space.Find("walkable")->AsBool()) << JsonStringify(space);

        Ok("opening.create", R"({"at": [8.125, 1, 3], "width": 1.2, "height": 2.2})");
        graph = Ok("space.graph", "{}");
        EXPECT_EQ(Connections(graph), (std::vector<std::string>{ "Hall>Yard:reached", "Yard>Hall:reached" }));
        ASSERT_EQ(graph.Find("openings")->AsArray().size(), 1u);
        EXPECT_TRUE(graph.Find("openings")->AsArray()[0].Find("crossed")->AsBool());
        EXPECT_TRUE(graph.Find("complete")->AsBool());
    }

    TEST_F(SpaceGraphTest, AJumpOfAnUnknownKindIsAConnectionThatDependsOnIt)
    {
        Ok("space.create", R"({"name": "Shed", "min": [20, 0, 0], "max": [26, 3, 6]})");
        EditorScene& scene = World.FocusDocument().GetScene();
        const Vec3d entry(15.0f, 0.0f, 3.0f);
        const EntityId jump = scene.CreateEntity(entry);
        NavLink link;
        link.Id = NavLinkId{ 0x1234 };
        link.Traversal = NavTraversalName("navigation.traversal.jump");
        link.ExitOffset = Vec3d(21.0f, 0.0f, 3.0f) - entry;
        scene.GetRegistry().Components.AddComponent(jump, link);

        EXPECT_EQ(Connections(Ok("space.graph", "{}")), (std::vector<std::string>{ "Yard>Shed:depends_on" }));
        EXPECT_EQ(Connections(Ok("space.graph", R"({"held": ["navigation.traversal.jump"]})")),
                  (std::vector<std::string>{ "Yard>Shed:reached" }));
        EXPECT_TRUE(Connections(Ok("space.graph", R"({"denied": ["navigation.traversal.jump"]})")).empty());
    }
}
