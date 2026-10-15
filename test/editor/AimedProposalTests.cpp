// Milestone M2's acceptance, headless: the designer aims at a wall, a client
// proposes a doorway "here" from view.context, and accept succeeds over an
// edit elsewhere but reports a conflict over an edit to that wall.

#include "WorkspaceFixture.h"

#include "document/EntityChangeSet.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"
#include "operations/SpaceEdits.h"
#include "operations/ViewOperations.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <gtest/gtest.h>

namespace
{
class AimedProposalTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Workspace.Level, Operations), errors));
        ASSERT_TRUE(Operations.Register(
            "view", ViewOperations({ Workspace.World, Workspace.Layout, Workspace.Selection, Workspace.Commands }), errors));
        Workspace.Layout.ActivateFacing(ViewportOrientation::Perspective);
        Workspace.Layout.OnResize(800, 600);
        Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
        Ok("space.create", R"({"name": "Shed", "min": [20, 0, 0], "max": [24, 3, 4]})");

        EditorCamera& eye = Workspace.Layout.Active()->Camera;
        eye.Position = { 4.0f, 1.5f, 3.0f };
        eye.Yaw = 0.0f;
        eye.Pitch = 0.0f;
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

    static std::string Code(const JsonValue& answer)
    {
        const JsonValue* code = answer.Find("code");
        return code != nullptr ? code->AsString() : std::string{};
    }

    // What a client does on "put a doorway here".
    void ProposeADoorwayHere()
    {
        const JsonValue aim = *Ok("view.context", "{}").Find("aim");
        ASSERT_TRUE(aim.IsObject());
        Ok("draft.begin", R"({"label": "Doorway here"})");
        Ok("opening.create", R"({"at": )" + JsonStringify(*aim.Find("point")) + "}");
    }

    // The designer's own edit, as the move gizmo makes it: through the level.
    void DesignerMoves(std::string_view name, const Vec3d& offset)
    {
        const EditorOperationOutcome moved =
            Workspace.Level.MutateLevel([&](EditorDocument& document, EntityChangeRecorder& recorder) {
                const EntityId entity = ResolveEntitySubject(document, "@" + std::string(name)).Entities.front();
                Transform3f placed = *document.GetScene().TryGetWorldTransform(entity);
                placed.Position = placed.Position + offset;
                const EntityMove move{ entity, placed };
                MoveReport report;
                return MoveEntities(document, recorder, std::span(&move, 1), false, report);
            });
        ASSERT_EQ(moved.Status, EditorOperationStatus::Ok) << moved.Message;
    }

    EditorOperations Operations;
};
}

TEST_F(AimedProposalTest, AnEditElsewhereLeavesTheProposalAcceptable)
{
    ProposeADoorwayHere();
    DesignerMoves("Shed", { 0, 0, 5 });
    Ok("draft.accept", "{}");
    EXPECT_TRUE(ResolveEntitySubject(Workspace.ActiveDocument(), "@Hall door").Resolved());
}

TEST_F(AimedProposalTest, AnEditToThatWallMakesAcceptReportAConflict)
{
    ProposeADoorwayHere();
    DesignerMoves("Hall", { 0, 0, -1 });
    const JsonValue answer = Call("draft.accept", "{}");
    EXPECT_EQ(Code(answer), "draft.conflict") << JsonStringify(answer);
    EXPECT_FALSE(ResolveEntitySubject(Workspace.ActiveDocument(), "@Hall door").Resolved());
}
