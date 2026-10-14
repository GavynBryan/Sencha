// changes.mark and changes.since: what changed in the level since a client last
// looked, whoever changed it, and never what a draft only proposes.

#include "WorkspaceFixture.h"

#include "document/EntityChangeSet.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <gtest/gtest.h>

#include <set>

namespace
{
class LevelChangesTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Workspace.Level, Operations), errors));
    }

    JsonValue Call(std::string_view name, std::string_view arguments)
    {
        return Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
    }

    JsonValue Ok(std::string_view name, std::string_view arguments)
    {
        JsonValue answer = Call(name, arguments);
        EXPECT_EQ(answer.Find("status")->AsString(), "ok") << name << ": " << JsonStringify(answer);
        const JsonValue* data = answer.Find("data");
        return data != nullptr ? *data : JsonValue{};
    }

    std::string Mark() { return Ok("changes.mark", "{}").Find("mark")->AsString(); }

    JsonValue Since(const std::string& mark) { return Ok("changes.since", R"({"mark": ")" + mark + R"("})"); }

    static std::string Code(const JsonValue& answer)
    {
        const JsonValue* code = answer.Find("code");
        return code != nullptr ? code->AsString() : std::string{};
    }

    static std::set<std::string> Names(const JsonValue& changes, std::string_view key)
    {
        std::set<std::string> names;
        for (const JsonValue& entity : changes.Find(key)->AsArray())
            names.insert(entity.Find("name")->AsString());
        return names;
    }

    void Delete(std::string_view name)
    {
        const EditorOperationOutcome deleted = Workspace.Level.Mutate(
            "test.delete", JsonValue{}, [name](EditorDocument& document, EntityChangeRecorder& recorder) {
                const EntityId entity = ResolveEntitySubject(document, "@" + std::string(name)).Entities.front();
                recorder.Touch(entity);
                document.GetScene().DestroyEntity(entity);
                return EditorOperationOutcome::Ok({});
            });
        ASSERT_EQ(deleted.Status, EditorOperationStatus::Ok);
    }

    EditorOperations Operations;
};
}

TEST_F(LevelChangesTest, ItReportsWhatWasCreatedChangedAndDeletedSinceTheMark)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6], "shell": false})");
    Ok("space.create", R"({"name": "Shed", "min": [20, 0, 0], "max": [24, 3, 4], "shell": false})");
    const std::string mark = Mark();

    Ok("space.create", R"({"name": "Yard", "min": [40, 0, 0], "max": [48, 3, 6], "shell": false})");
    Ok("space.move", R"({"subject": "@Hall", "offset": [0, 0, -2]})");
    Delete("Shed");

    const JsonValue changes = Since(mark);
    EXPECT_EQ(Names(changes, "created"), std::set<std::string>{ "Yard" });
    EXPECT_EQ(Names(changes, "modified"), std::set<std::string>{ "Hall" });
    EXPECT_EQ(Names(changes, "deleted"), std::set<std::string>{ "Shed" }) << "a deleted entity keeps the name it had";

    const std::string now = changes.Find("mark")->AsString();
    EXPECT_NE(now, mark);
    const JsonValue quiet = Since(now);
    EXPECT_TRUE(quiet.Find("created")->AsArray().empty());
    EXPECT_TRUE(quiet.Find("modified")->AsArray().empty());
    EXPECT_TRUE(quiet.Find("deleted")->AsArray().empty());
    EXPECT_EQ(quiet.Find("mark")->AsString(), now) << "nothing changed, so the mark still holds";
}

TEST_F(LevelChangesTest, ADraftIsNotTheLevelUntilItIsAcceptedAndAnUndoIsAChange)
{
    const std::string mark = Mark();
    Ok("draft.begin", R"({"label": "Hall"})");
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6], "shell": false})");
    EXPECT_TRUE(Since(mark).Find("created")->AsArray().empty()) << "only proposed";

    Ok("draft.accept", "{}");
    const JsonValue accepted = Since(mark);
    EXPECT_EQ(Names(accepted, "created"), std::set<std::string>{ "Hall" });

    Commands.Undo();
    EXPECT_EQ(Names(Since(accepted.Find("mark")->AsString()), "deleted"), std::set<std::string>{ "Hall" });
}

TEST_F(LevelChangesTest, AnOldMarkExpiresAndAReplacedLevelIsSaid)
{
    EXPECT_EQ(Code(Call("changes.since", R"({"mark": "nope"})")), "changes.unknown_mark");
    const std::string first = Mark();
    for (int index = 0; index < 16; ++index)
        Mark();
    EXPECT_EQ(Code(Call("changes.since", R"({"mark": ")" + first + R"("})")), "changes.unknown_mark");

    const std::string held = Mark();
    Workspace.World.NewWorld("Elsewhere");
    EXPECT_EQ(Code(Call("changes.since", R"({"mark": ")" + held + R"("})")), "changes.level_replaced");
}
