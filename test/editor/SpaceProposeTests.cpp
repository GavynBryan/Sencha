// space.propose: spaces for a level built without them, read off its
// navigation, proposed in a draft and leaving alone what spaces already hold.

#include "NavigationSettingsFixture.h"

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/SpaceComponents.h"
#include "document/WorldDocument.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    class SpaceProposeTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_space_propose_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            WriteTestNavigationSettings(Root);
            World.SetContentRoots({ Root });
            std::vector<std::string> errors;
            ASSERT_TRUE(Operations.Register("level", LevelOperations(Level, Operations), errors));

            // Two 8 m rooms' floors and the 1.5 m corridor between them, built by hand.
            EditorScene& scene = World.FocusDocument().GetScene();
            (void)scene.CreateBrush(Vec3d{ 4, -0.5f, 4 }, Vec3d{ 4, 0.5f, 4 });
            (void)scene.CreateBrush(Vec3d{ 10, -0.5f, 4 }, Vec3d{ 2, 0.5f, 0.75f });
            (void)scene.CreateBrush(Vec3d{ 16, -0.5f, 4 }, Vec3d{ 4, 0.5f, 4 });
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

        static std::vector<std::string> Kinds(const JsonValue& proposal)
        {
            std::vector<std::string> kinds;
            for (const JsonValue& space : proposal.Find("proposed")->AsArray())
                kinds.push_back(space.Find("kind")->AsString());
            return kinds;
        }

        bool Exists(std::string_view name)
        {
            return ResolveEntitySubject(World.FocusDocument(), "@" + std::string(name)).Resolved();
        }

        fs::path Root;
        LoggingProvider Logging;
        WorldDocument World{ Logging };
        CommandStack Commands;
        LevelTarget Level{ World, Commands };
        EditorOperations Operations;
    };

    TEST_F(SpaceProposeTest, TwoRoomsAndThePassageBetweenAreProposedInADraft)
    {
        Ok("draft.begin", R"({"label": "Annotate"})");
        const JsonValue proposal = Ok("space.propose", "{}");
        EXPECT_EQ(Kinds(proposal), (std::vector<std::string>{ "room", "room", "passage" }));
        EXPECT_FALSE(Exists("Room")) << "only proposed";

        Ok("draft.accept", "{}");
        ASSERT_TRUE(Exists("Room"));
        const EntityId room = ResolveEntitySubject(World.FocusDocument(), "@Room").Entities.front();
        EXPECT_FALSE(World.FocusDocument().GetScene().GetRegistry().Components.HasComponent<Shell>(room))
            << "the level already has its own walls";
        EXPECT_TRUE(Kinds(Ok("space.propose", "{}")).empty()) << "everything walkable is held now";
    }

    TEST_F(SpaceProposeTest, WhatASpaceAlreadyHoldsIsLeftAlone)
    {
        Ok("space.create", R"({"name": "West", "min": [0, 0, 0], "max": [8, 3, 8], "shell": false})");
        EXPECT_EQ(Kinds(Ok("space.propose", "{}")), (std::vector<std::string>{ "room", "passage" }));
    }
}
