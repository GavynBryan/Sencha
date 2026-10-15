// Notes: left about an entity or at a point, following what they are about,
// listed open or resolved, saved with the level and undone like any edit.

#include "WorkspaceFixture.h"

#include "document/NoteComponent.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

namespace
{
class NoteOperationsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Workspace.Level, Operations), errors));
        Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
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

    JsonValue::Array Notes(std::string_view state = "open")
    {
        return Ok("notes.list", R"({"state": ")" + std::string(state) + R"("})").Find("notes")->AsArray();
    }

    EditorOperations Operations;
};
}

TEST_F(NoteOperationsTest, ANoteAboutASpaceFollowsItAndIsListedOpen)
{
    Ok("note.add", R"({"text": "Too dark for the jump?", "subject": "@Hall"})");
    JsonValue::Array open = Notes();
    ASSERT_EQ(open.size(), 1u);
    EXPECT_EQ(open[0].Find("text")->AsString(), "Too dark for the jump?");
    EXPECT_EQ(open[0].Find("author")->AsString(), "claude");
    EXPECT_EQ(open[0].Find("about")->Find("name")->AsString(), "Hall");

    Ok("space.move", R"({"subject": "@Hall", "offset": [10, 0, 0]})");
    EXPECT_NEAR(Notes()[0].Find("at")->AsArray()[0].AsNumber(), 14.0, 1e-3) << "it moved with the hall";
}

TEST_F(NoteOperationsTest, AResolvedNoteLeavesTheOpenList)
{
    Ok("note.add", R"({"text": "Check the door width", "at": [2, 0, 2], "author": "gavyn"})");
    Ok("note.resolve", R"({"subject": "@Note"})");
    EXPECT_TRUE(Notes().empty());
    ASSERT_EQ(Notes("resolved").size(), 1u);
    EXPECT_TRUE(Notes("resolved")[0].Find("about")->IsNull());
    Commands.Undo();
    EXPECT_EQ(Notes().size(), 1u) << "resolving is one undo step";
    EXPECT_EQ(Code(Call("note.resolve", R"({"subject": "@Hall"})")), "note.not_note");
}

TEST_F(NoteOperationsTest, ANoteIsSavedWithTheLevelAndUndoneLikeAnyEdit)
{
    Ok("note.add", R"({"text": "Widen this", "subject": "@Hall"})");
    const std::string saved = Workspace.ActiveDocument().ToSceneText();
    EditorDocument reloaded(Logging);
    std::string error;
    ASSERT_TRUE(reloaded.LoadFromSceneText(saved, &error)) << error;
    bool found = false;
    for (const EntityId entity : reloaded.GetScene().GetAllEntities())
        if (const Note* note = reloaded.GetScene().GetRegistry().Components.TryGet<Note>(entity))
            found = found || note->Text.View() == "Widen this";
    EXPECT_TRUE(found);

    Commands.Undo();
    EXPECT_TRUE(Notes("all").empty());
    EXPECT_EQ(Code(Call("note.add", R"({"text": ""})")), "note.text_invalid");
    EXPECT_EQ(Code(Call("note.add", R"({"text": "Where?"})")), "note.nowhere");
}
