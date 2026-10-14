// Level mutations edit the live document as one undo step each and answer with
// what changed, by persistent id, and the revisions either side.

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "document/WorldDocument.h"
#include "document/commands/RenameEntityCommand.h"
#include "operations/EditorOperations.h"
#include "operations/LevelMutations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    class LevelMutationsTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            std::vector<std::string> errors;
            ASSERT_TRUE(Operations.Register("level", LevelMutationOperations(Level), errors));
            EditorDocument& document = World.FocusDocument();
            Hall = document.GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
            MakeRenameEntityCommand(Hall, "Hall", document.GetScene(), document)->Execute();
        }

        std::string Name(EntityId entity)
        {
            const auto* name = World.FocusDocument().GetRegistry().Components.TryGet<EntityNameComponent>(entity);
            return name != nullptr ? std::string(name->Value.View()) : std::string{};
        }

        JsonValue Call(std::string_view name, std::string_view arguments)
        {
            return Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
        }

        static std::string Status(const JsonValue& answer) { return answer.Find("status")->AsString(); }
        static std::string Code(const JsonValue& answer) { return answer.Find("code")->AsString(); }

        LoggingProvider Logging;
        WorldDocument World{ Logging };
        CommandStack Commands;
        LevelTarget Level{ World, Commands };
        EditorOperations Operations;
        EntityId Hall;
    };

    TEST_F(LevelMutationsTest, ARenameIsOneUndoStepAndSaysWhatItChanged)
    {
        const std::uint64_t revision = World.FocusDocument().Revision();
        const JsonValue answer = Call("entity.rename", R"({"subject": "@Hall", "name": "Atrium"})");
        ASSERT_EQ(Status(answer), "ok") << JsonStringify(answer);
        EXPECT_EQ(Name(Hall), "Atrium");

        const JsonValue& data = *answer.Find("data");
        ASSERT_EQ(data.Find("modified")->AsArray().size(), 1u);
        const JsonValue& modified = data.Find("modified")->AsArray().front();
        EXPECT_EQ(modified.Find("id")->AsString(), PersistentIdText(World.FocusDocument(), Hall));
        EXPECT_EQ(modified.Find("name")->AsString(), "Atrium");
        EXPECT_TRUE(data.Find("created")->AsArray().empty());
        EXPECT_TRUE(data.Find("deleted")->AsArray().empty());
        EXPECT_EQ(data.Find("revision")->Find("before")->AsNumber(), static_cast<double>(revision));
        EXPECT_GT(data.Find("revision")->Find("after")->AsNumber(), static_cast<double>(revision));

        Commands.Undo();
        EXPECT_EQ(Name(Hall), "Hall");
        EXPECT_FALSE(Commands.CanUndo());
        Commands.Redo();
        EXPECT_EQ(Name(Hall), "Atrium");
    }

    TEST_F(LevelMutationsTest, TheSameNameChangesNothingAndLeavesNoStep)
    {
        const JsonValue answer = Call("entity.rename", R"({"subject": "@Hall", "name": " Hall "})");
        ASSERT_EQ(Status(answer), "ok") << JsonStringify(answer);
        EXPECT_TRUE(answer.Find("data")->Find("modified")->AsArray().empty());
        EXPECT_FALSE(Commands.CanUndo());
    }

    TEST_F(LevelMutationsTest, AnEmptyNameClearsIt)
    {
        ASSERT_EQ(Status(Call("entity.rename", R"({"subject": "@Hall", "name": "  "})")), "ok");
        EXPECT_EQ(World.FocusDocument().GetRegistry().Components.TryGet<EntityNameComponent>(Hall), nullptr);
        Commands.Undo();
        EXPECT_EQ(Name(Hall), "Hall");
    }

    TEST_F(LevelMutationsTest, ARefusedRenameTouchesNothing)
    {
        EXPECT_EQ(Code(Call("entity.rename", R"({"subject": "@Nowhere", "name": "Atrium"})")), "subject.not_found");
        const std::string tooLong(64, 'a');
        const JsonValue answer = Call("entity.rename", R"({"subject": "@Hall", "name": ")" + tooLong + R"("})");
        EXPECT_EQ(Status(answer), "refused");
        EXPECT_EQ(Code(answer), "entity.name_invalid");
        EXPECT_EQ(Name(Hall), "Hall");
        EXPECT_FALSE(Commands.CanUndo());
    }
    TEST_F(LevelMutationsTest, AMutationRefusedHalfwayPutsBackWhatItChanged)
    {
        const EditorOperationOutcome outcome =
            Level.Mutate("test.halfway", JsonValue{}, [this](EditorDocument& document, EntityChangeRecorder& recorder) {
                recorder.Touch(Hall);
                MakeRenameEntityCommand(Hall, "Atrium", document.GetScene(), document)->Execute();
                return EditorOperationOutcome::Refused("test.halfway", "refused after editing");
            });
        EXPECT_EQ(outcome.Status, EditorOperationStatus::Refused);
        EXPECT_EQ(Name(Hall), "Hall");
        EXPECT_FALSE(Commands.CanUndo());
    }
}
