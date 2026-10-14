// Drafts: mutations edit a copy of the level, which is accepted as one undo
// step, discarded, refused over a competing edit and rebased by replay.

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "document/WorldDocument.h"
#include "document/commands/RenameEntityCommand.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    class LevelDraftsTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            std::vector<std::string> errors;
            ASSERT_TRUE(Operations.Register("level", LevelOperations(Level, Operations), errors));
            Hall = Brush(Vec3d{ 0, 0, 0 }, "Hall");
            HallId = PersistentIdText(World.FocusDocument(), Hall);
        }

        EntityId Brush(const Vec3d& center, const char* name)
        {
            EditorDocument& document = World.FocusDocument();
            const EntityId entity = document.GetScene().CreateBrush(center, Vec3d{ 2, 1, 2 });
            MakeRenameEntityCommand(entity, name, document.GetScene(), document)->Execute();
            return entity;
        }

        std::string Name(EntityId entity)
        {
            const auto* name = World.FocusDocument().GetRegistry().Components.TryGet<EntityNameComponent>(entity);
            return name != nullptr ? std::string(name->Value.View()) : std::string{};
        }

        JsonValue Call(std::string_view name, std::string_view arguments = "{}")
        {
            return Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
        }

        JsonValue Ok(std::string_view name, std::string_view arguments = "{}")
        {
            JsonValue answer = Call(name, arguments);
            EXPECT_EQ(answer.Find("status")->AsString(), "ok") << name << ": " << JsonStringify(answer);
            const JsonValue* data = answer.Find("data");
            return data != nullptr ? *data : JsonValue{};
        }

        static std::string Code(const JsonValue& answer)
        {
            const JsonValue* code = answer.Find("code");
            return code != nullptr ? code->AsString() : std::string{};
        }

        std::string RenameHall(std::string_view name)
        {
            return R"({"subject": ")" + HallId + R"(", "name": ")" + std::string(name) + R"("})";
        }

        LoggingProvider Logging;
        WorldDocument World{ Logging };
        CommandStack Commands;
        LevelTarget Level{ World, Commands };
        EditorOperations Operations;
        EntityId Hall;
        std::string HallId;
    };

    TEST_F(LevelDraftsTest, ADraftIsEditedAndReadUntilItIsAcceptedAsOneStep)
    {
        EXPECT_EQ(Ok("draft.begin", R"({"label": "Rename the hall"})").Find("draft")->AsString(), "d1");
        Ok("entity.rename", RenameHall("Atrium"));
        EXPECT_EQ(Name(Hall), "Hall") << "the level never saw the draft's edit";
        EXPECT_FALSE(Commands.CanUndo());
        EXPECT_EQ(Ok("entity.describe", R"({"subject": "@Atrium"})").Find("entity")->Find("id")->AsString(), HallId)
            << "queries read the draft being written";

        const JsonValue diff = Ok("draft.diff");
        ASSERT_EQ(diff.Find("modified")->AsArray().size(), 1u);
        EXPECT_EQ(diff.Find("modified")->AsArray().front().Find("name")->AsString(), "Atrium");
        ASSERT_EQ(diff.Find("steps")->AsArray().size(), 1u);
        EXPECT_EQ(diff.Find("steps")->AsArray().front().Find("operation")->AsString(), "entity.rename");

        const JsonValue accepted = Ok("draft.accept");
        EXPECT_EQ(accepted.Find("modified")->AsArray().size(), 1u);
        EXPECT_EQ(Name(Hall), "Atrium");
        EXPECT_TRUE(Ok("draft.list").Find("drafts")->AsArray().empty());

        Commands.Undo();
        EXPECT_EQ(Name(Hall), "Hall");
        EXPECT_FALSE(Commands.CanUndo()) << "the whole draft was one step";
        Commands.Redo();
        EXPECT_EQ(Name(Hall), "Atrium");
    }

    TEST_F(LevelDraftsTest, ADiscardedDraftLeavesNothingAndMutationsGoBackToTheLevel)
    {
        Ok("draft.begin", R"({"label": "Try a name"})");
        Ok("entity.rename", RenameHall("Atrium"));
        Ok("draft.discard");
        EXPECT_EQ(Name(Hall), "Hall");
        EXPECT_FALSE(Commands.CanUndo());

        Ok("entity.rename", RenameHall("Lobby"));
        EXPECT_EQ(Name(Hall), "Lobby");
        EXPECT_TRUE(Commands.CanUndo());
    }

    TEST_F(LevelDraftsTest, ACompetingEditIsAConflictThatARebaseTakesIn)
    {
        const EntityId other = Brush(Vec3d{ 9, 0, 0 }, "Store");
        Ok("draft.begin", R"({"label": "Rename the hall"})");
        Ok("entity.rename", RenameHall("Atrium"));

        Ok("draft.write");
        Ok("entity.rename", RenameHall("Lobby"));
        Ok("entity.rename", R"({"subject": "@Store", "name": "Pantry"})");

        const JsonValue refused = Call("draft.accept", R"({"draft": "d1"})");
        EXPECT_EQ(Code(refused), "draft.conflict");
        EXPECT_EQ(refused.Find("detail")->Find("entities")->AsArray().front().AsString(), HallId);
        EXPECT_EQ(Name(Hall), "Lobby");

        Ok("draft.rebase", R"({"draft": "d1"})");
        Ok("draft.accept", R"({"draft": "d1"})");
        EXPECT_EQ(Name(Hall), "Atrium");
        EXPECT_EQ(Name(other), "Pantry") << "the rebase kept the level's own edits";
    }

    TEST_F(LevelDraftsTest, ARefusedStepFailsTheDraftUntilItIsResumed)
    {
        Ok("draft.begin", R"({"label": "Two renames"})");
        Ok("entity.rename", RenameHall("Atrium"));
        EXPECT_EQ(Code(Call("entity.rename", R"({"subject": "@Nowhere", "name": "X"})")), "subject.not_found");

        EXPECT_EQ(Ok("draft.diff").Find("state")->AsString(), "failed");
        EXPECT_EQ(Code(Call("entity.rename", RenameHall("Lobby"))), "draft.failed");
        EXPECT_EQ(Code(Call("draft.accept")), "draft.failed");

        Ok("draft.resume");
        Ok("entity.rename", RenameHall("Lobby"));
        const JsonValue diff = Ok("draft.diff");
        EXPECT_EQ(diff.Find("steps")->AsArray().size(), 2u) << "the refused step is not one of the draft's";
        Ok("draft.accept");
        EXPECT_EQ(Name(Hall), "Lobby");
    }

    TEST_F(LevelDraftsTest, EditsThatCancelOutLeaveNoChange)
    {
        Ok("draft.begin", R"({"label": "There and back"})");
        Ok("entity.rename", RenameHall("Atrium"));
        Ok("entity.rename", RenameHall("Hall"));
        const JsonValue diff = Ok("draft.diff");
        EXPECT_TRUE(diff.Find("modified")->AsArray().empty());
        EXPECT_EQ(diff.Find("steps")->AsArray().size(), 2u);
    }

    TEST_F(LevelDraftsTest, DraftsWithOneSeedMintTheSameIds)
    {
        Ok("draft.begin", R"({"label": "First", "id_seed": 7})");
        Ok("draft.begin", R"({"label": "Second", "id_seed": 7})");
        EXPECT_EQ(Level.FindDraft("d1")->Document->GetScene().MintPersistentId(),
                  Level.FindDraft("d2")->Document->GetScene().MintPersistentId());
    }

    TEST_F(LevelDraftsTest, TheCopyHoldsEveryEntityExactly)
    {
        EditorDocument& level = World.FocusDocument();
        const EntityId child = level.GetScene().CreateEntity(Vec3d{ 1, 2, 3 });
        ASSERT_TRUE(level.GetScene().SetParent(child, Hall));
        Ok("draft.begin", R"({"label": "Copy"})");
        EditorDocument& copy = *Level.WritingDraft()->Document;

        std::size_t compared = 0;
        for (const EntityId entity : { Hall, child })
        {
            const PersistentEntityId id = level.GetScene().PersistentIdOf(entity);
            EntityChangeSet probe;
            probe.Changes.push_back({ id, level.CaptureEntity(entity), std::nullopt });
            EXPECT_TRUE(ChangeSetConflicts(copy, probe).empty())
                << PersistentEntityIdToString(id) << " differs in the copy";
            ++compared;
        }
        EXPECT_EQ(compared, 2u);
    }
}
