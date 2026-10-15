// The brush operations: a box between corners, a face pulled out, a plane
// cut and a shape carved, each one undo step, and the Brush tool's commit
// made through the same level target.

#include "WorkspaceFixture.h"

#include "document/tools/BrushTool.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"
#include "tools/ToolRegistry.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <gtest/gtest.h>

namespace
{
class BrushOperationsTest : public WorkspaceTest
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

    static std::string Code(const JsonValue& answer)
    {
        const JsonValue* code = answer.Find("code");
        return code != nullptr ? code->AsString() : std::string{};
    }

    EntityId Find(std::string_view name)
    {
        const SubjectResolution found = ResolveEntitySubject(Workspace.ActiveDocument(), "@" + std::string(name));
        return found.Resolved() ? found.Entities.front() : EntityId{};
    }

    JsonValue Bounds(std::string_view name)
    {
        return *Ok("entity.describe", R"({"subject": "@)" + std::string(name) + R"("})").Find("entity");
    }

    static double At(const JsonValue& entity, std::string_view corner, std::size_t axis)
    {
        return entity.Find(corner)->AsArray()[axis].AsNumber();
    }

    std::size_t Faces(std::string_view name)
    {
        return Scene().TryGetBrushMesh(Find(name))->Faces.size();
    }

    EditorOperations Operations;
};
}

TEST_F(BrushOperationsTest, ABoxGoesBetweenItsCornersWhicheverWayRoundTheyAre)
{
    const JsonValue made = Ok("brush.box", R"({"min": [3, 2, 1], "max": [1, 0, 0], "name": "Pillar"})");
    EXPECT_EQ(made.Find("brush")->Find("name")->AsString(), "Pillar");
    const JsonValue pillar = Bounds("Pillar");
    EXPECT_NEAR(At(pillar, "min", 0), 1.0, 1e-3);
    EXPECT_NEAR(At(pillar, "max", 1), 2.0, 1e-3);
    EXPECT_EQ(Code(Call("brush.box", R"({"min": [0, 0, 0], "max": [1, 0, 1]})")), "brush.too_small");

    Ok("brush.box", R"({"min": [5, 0, 0], "max": [6, 1, 1]})");
    EXPECT_TRUE(Find("Brush").IsValid()) << "an unnamed box takes the next free brush name";
    Commands.Undo();
    EXPECT_FALSE(Find("Brush").IsValid());
}

TEST_F(BrushOperationsTest, AFacePulledOutGrowsTheBrushThatWay)
{
    Ok("brush.box", R"({"min": [0, 0, 0], "max": [2, 1, 2], "name": "Step"})");
    Ok("brush.extrude", R"({"subject": "@Step", "face": "up", "distance": 0.5})");
    EXPECT_NEAR(At(Bounds("Step"), "max", 1), 1.5, 1e-3);
    Commands.Undo();
    EXPECT_NEAR(At(Bounds("Step"), "max", 1), 1.0, 1e-3);

    Ok("space.create", R"({"name": "Hall", "min": [10, 0, 0], "max": [14, 3, 4]})");
    EXPECT_EQ(Code(Call("brush.extrude", R"({"subject": "@Hall", "face": "up", "distance": 1})")), "brush.not_solid")
        << "a space's faces move with space.resize_face";
}

TEST_F(BrushOperationsTest, AClipKeepsASideOrSplitsIntoTwoBrushes)
{
    Ok("brush.box", R"({"min": [0, 0, 0], "max": [4, 2, 2], "name": "Block"})");
    Ok("brush.clip", R"({"subjects": ["@Block"], "point": [1, 0, 0], "normal": [1, 0, 0]})");
    EXPECT_NEAR(At(Bounds("Block"), "min", 0), 1.0, 1e-3) << "the side the normal points to stays";

    const JsonValue split = Ok("brush.clip",
        R"({"subjects": ["@Block"], "point": [3, 0, 0], "normal": [1, 0, 0], "keep": "both"})");
    ASSERT_EQ(split.Find("pieces")->AsArray().size(), 2u) << JsonStringify(split);
    const std::string other = split.Find("pieces")->AsArray()[1].Find("name")->AsString();
    EXPECT_NEAR(At(Bounds("Block"), "max", 0), 4.0, 1e-3) << "the brush keeps the front";
    EXPECT_NEAR(At(Bounds(other), "max", 0), 3.0, 1e-3) << "the back is a brush of its own";

    EXPECT_EQ(Code(Call("brush.clip", R"({"subjects": ["@Block"], "point": [9, 0, 0], "normal": [1, 0, 0]})")),
              "clip.missed");
    Commands.Undo();
    EXPECT_FALSE(Find(other).IsValid());
}

TEST_F(BrushOperationsTest, ACarveCutsAWindowThroughAWall)
{
    Ok("brush.box", R"({"min": [0, 0, 0], "max": [4, 3, 0.25], "name": "Wall"})");
    const std::size_t before = Faces("Wall");
    Ok("brush.carve", R"({"subject": "@Wall", "at": [2, 1.5, 0.25], "width": 1, "height": 1})");
    EXPECT_GT(Faces("Wall"), before) << "the window's jambs, sill and head are faces";
    EXPECT_EQ(Code(Call("brush.carve", R"({"subject": "@Wall", "at": [2, 1.5, 5], "width": 1, "height": 1})")),
              "carve.no_face");
    Commands.Undo();
    EXPECT_EQ(Faces("Wall"), before);
}

TEST_F(BrushOperationsTest, TheBrushToolsCommitIsOneLevelEditUndoneAndRedone)
{
    ToolRegistry& tools = *Workspace.Interaction.Tools;
    BrushTool* tool = nullptr;
    for (const std::unique_ptr<ITool>& candidate : tools.GetTools())
        if (candidate->GetId() == "brush")
            tool = static_cast<BrushTool*>(candidate.get());
    ASSERT_NE(tool, nullptr);

    BrushTool::PendingBrush pending;
    pending.Center = { 0, 0.5f, 0 };
    pending.HalfExtents = { 1, 0.5f, 1 };
    pending.DragDepthHalf = 0.5f;
    tool->SetPending(tools.GetContext(), pending);
    tool->CommitPending(tools.GetContext());
    ASSERT_TRUE(Find("Brush").IsValid());

    Commands.Undo();
    EXPECT_FALSE(Find("Brush").IsValid());
    EXPECT_TRUE(Workspace.Selection.GetSelection().empty()) << "nothing undone stays selected";
    Commands.Redo();
    EXPECT_TRUE(Find("Brush").IsValid());
}
