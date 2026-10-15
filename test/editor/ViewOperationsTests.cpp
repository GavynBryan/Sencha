// The view operations: what the designer is looking at, read from the active
// view, and a selection and framing a client hands back.

#include "WorkspaceFixture.h"

#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/SpaceRelations.h"
#include "operations/ViewOperations.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <gtest/gtest.h>

namespace
{
class ViewOperationsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Workspace.Level, Operations), errors));
        ASSERT_TRUE(Operations.Register("view", ViewOperations({ Workspace.World, Workspace.Layout, Workspace.Selection, Workspace.Commands }), errors));
        Workspace.Layout.ActivateFacing(ViewportOrientation::Perspective);
        Workspace.Layout.OnResize(800, 600);
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

    static std::string NameIn(const JsonValue& value, std::string_view key)
    {
        const JsonValue* entity = value.Find(key);
        return entity != nullptr && entity->IsObject() ? entity->Find("name")->AsString() : std::string{};
    }

    EditorCamera& Eye() { return Workspace.Layout.Active()->Camera; }

    EditorOperations Operations;
};
}

TEST_F(ViewOperationsTest, TheAimFindsTheWallAheadAndADoorGoesWhereItLands)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Eye().Position = { 4.0f, 1.5f, 3.0f };
    Eye().Yaw = 0.0f;
    Eye().Pitch = 0.0f;

    const JsonValue context = Ok("view.context", "{}");
    const JsonValue& camera = *context.Find("camera");
    EXPECT_EQ(camera.Find("view")->AsString(), "perspective");
    EXPECT_EQ(camera.Find("facing")->AsString(), "n") << "the camera looks along -Z";
    EXPECT_EQ(NameIn(context, "in_space"), "Hall");

    const JsonValue& aim = *context.Find("aim");
    ASSERT_TRUE(aim.IsObject()) << JsonStringify(context);
    EXPECT_EQ(NameIn(aim, "entity"), "Hall north wall");
    EXPECT_EQ(aim.Find("kind")->AsString(), "wall");
    EXPECT_EQ(NameIn(aim, "facing_into"), "Hall");
    EXPECT_NEAR(aim.Find("point")->AsArray()[2].AsNumber(), 0.0, 1e-3) << "the north wall's inner face";
    EXPECT_NEAR(aim.Find("normal")->AsArray()[2].AsNumber(), 1.0, 1e-3);
    EXPECT_NEAR(aim.Find("distance")->AsNumber(), 3.0, 1e-3);

    const JsonValue door = Ok("opening.create", R"({"at": )" + JsonStringify(*aim.Find("point")) + "}");
    EXPECT_EQ(NameIn(door, "space"), "Hall") << "the aimed point is a place a door can go";
}

TEST_F(ViewOperationsTest, TheSpacesInViewAreListedNearestFirstAndNothingAimedIsNull)
{
    Ok("space.create", R"({"name": "Near", "min": [-2, 0, -12], "max": [2, 3, -8]})");
    Ok("space.create", R"({"name": "Far", "min": [-2, 0, -42], "max": [2, 3, -38]})");
    Ok("space.create", R"({"name": "Behind", "min": [-2, 0, 8], "max": [2, 3, 12]})");
    Eye().Position = { 0.0f, 4.0f, 0.0f };
    Eye().Yaw = 0.0f;
    Eye().Pitch = 0.0f;

    const JsonValue context = Ok("view.context", "{}");
    EXPECT_TRUE(context.Find("aim")->IsNull()) << "level with the eye and above every room";
    EXPECT_TRUE(context.Find("in_space")->IsNull());
    const JsonValue::Array& seen = context.Find("in_view")->AsArray();
    ASSERT_EQ(seen.size(), 2u) << JsonStringify(context);
    EXPECT_EQ(NameIn(seen[0], "space"), "Near");
    EXPECT_EQ(NameIn(seen[1], "space"), "Far");

    const JsonValue first = Ok("view.context", R"({"limit": 1})");
    EXPECT_EQ(first.Find("in_view")->AsArray().size(), 1u);
    EXPECT_TRUE(first.Find("truncated")->AsBool());
}

TEST_F(ViewOperationsTest, SelectingIsOneUndoStepAndFramesWhenAsked)
{
    Ok("space.create", R"({"name": "Hall", "min": [20, 0, 20], "max": [28, 3, 26]})");
    Eye().Position = { 0.0f, 2.0f, 0.0f };

    const JsonValue selected = Ok("view.select", R"({"subjects": ["@Hall"], "frame": true})");
    ASSERT_EQ(selected.Find("selection")->AsArray().size(), 1u);
    EXPECT_EQ(selected.Find("selection")->AsArray()[0].Find("element")->AsString(), "entity");
    EXPECT_TRUE(selected.Find("framed")->AsBool());
    EXPECT_GT(Eye().Position.X, 10.0f) << "the view turned onto the hall";

    const JsonValue context = Ok("view.context", "{}");
    EXPECT_EQ(context.Find("selection")->AsArray()[0].Find("name")->AsString(), "Hall");

    Commands.Undo();
    EXPECT_TRUE(Workspace.Selection.GetSelection().empty());
    EXPECT_EQ(Code(Call("view.select", R"({"subjects": ["@Nowhere"]})")), "subject.not_found");
}

TEST_F(ViewOperationsTest, FramingNamesWhatToFrameOrFallsBackToTheSelection)
{
    EXPECT_EQ(Code(Call("view.frame", "{}")), "view.nothing_to_frame");
    Ok("space.create", R"({"name": "Hall", "min": [20, 0, 20], "max": [28, 3, 26]})");
    Eye().Position = { 0.0f, 2.0f, 0.0f };
    Ok("view.frame", R"({"subjects": ["@Hall"]})");
    EXPECT_GT(Eye().Position.X, 10.0f);
    EXPECT_TRUE(Workspace.Selection.GetSelection().empty()) << "framing leaves the selection alone";
}

TEST_F(ViewOperationsTest, ThePointIsInTheSmallestSpaceHoldingIt)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [20, 6, 20], "shell": false})");
    Ok("space.create", R"({"name": "Booth", "min": [2, 0, 2], "max": [4, 3, 4], "shell": false})");
    const EditorScene& scene = Scene();
    const auto name = [&](const Vec3d& point) {
        const std::optional<EntityId> space = SpaceContaining(scene, point);
        return space ? EntityNameText(Workspace.ActiveDocument(), *space) : std::string{};
    };
    EXPECT_EQ(name({ 3, 1, 3 }), "Booth");
    EXPECT_EQ(name({ 10, 1, 10 }), "Hall");
    EXPECT_EQ(name({ 30, 1, 10 }), "");
}
