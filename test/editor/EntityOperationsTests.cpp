// entity.delete, entity.duplicate and entity.reparent: an entity means its
// branch, each is one undo step, and each refuses what it cannot do by code.

#include "WorkspaceFixture.h"

#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <gtest/gtest.h>

namespace
{
class EntityOperationsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Workspace.Level, Operations), errors));
        Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
        Ok("opening.create", R"({"at": [4, 1, -0.1], "name": "Door"})");
        Ok("space.create", R"({"name": "Shed", "min": [20, 0, 0], "max": [24, 3, 4]})");
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

    bool Exists(std::string_view name)
    {
        return ResolveEntitySubject(Workspace.ActiveDocument(), "@" + std::string(name)).Resolved();
    }

    EntityId Find(std::string_view name)
    {
        return ResolveEntitySubject(Workspace.ActiveDocument(), "@" + std::string(name)).Entities.front();
    }

    Vec3d WorldPosition(std::string_view name)
    {
        Scene().RefreshDerivedTransforms();
        return Scene().TryGetWorldTransform(Find(name))->Position;
    }

    EditorOperations Operations;
};
}

TEST_F(EntityOperationsTest, DeletingASpaceTakesItsDoorAndUndoBringsBothBack)
{
    const JsonValue deleted = Ok("entity.delete", R"({"subjects": ["@Hall"]})");
    EXPECT_FALSE(Exists("Hall"));
    EXPECT_FALSE(Exists("Door")) << "the door is under the hall";
    EXPECT_TRUE(Exists("Shed"));
    EXPECT_EQ(deleted.Find("deleted")->AsArray().size(), 2u) << JsonStringify(deleted);
    EXPECT_EQ(deleted.Find("walls")->Find("deleted")->AsNumber(), 12.0) << "and its walls, counted";

    Commands.Undo();
    EXPECT_TRUE(Exists("Hall"));
    EXPECT_TRUE(Exists("Door"));
    EXPECT_EQ(Code(Call("entity.delete", R"({"subjects": ["@Nowhere"]})")), "subject.not_found");
}

TEST_F(EntityOperationsTest, ACopyTakesItsBranchTheNextFreeNameAndTheOffset)
{
    const Vec3d door = WorldPosition("Door");
    const JsonValue duplicated = Ok("entity.duplicate", R"({"subjects": ["@Hall"], "offset": [0, 0, 20]})");
    const JsonValue::Array& copies = duplicated.Find("copies")->AsArray();
    ASSERT_EQ(copies.size(), 1u);
    EXPECT_EQ(copies[0].Find("name")->AsString(), "Hall 1");
    ASSERT_TRUE(Exists("Door 1")) << "the copy brings its own door";
    EXPECT_EQ(Scene().GetParent(Find("Door 1")), Find("Hall 1"));
    const Vec3d moved = WorldPosition("Door 1");
    EXPECT_NEAR(moved.X, door.X, 1e-4f);
    EXPECT_NEAR(moved.Z, door.Z + 20.0f, 1e-4f);
    EXPECT_NE(PersistentIdText(Workspace.ActiveDocument(), Find("Hall 1")),
              PersistentIdText(Workspace.ActiveDocument(), Find("Hall")));

    Commands.Undo();
    EXPECT_FALSE(Exists("Hall 1"));
    EXPECT_FALSE(Exists("Door 1"));
}

TEST_F(EntityOperationsTest, AReparentKeepsTheWorldPlacementAndRefusesACycle)
{
    const Vec3d shed = WorldPosition("Shed");
    Ok("entity.reparent", R"({"subjects": ["@Shed"], "parent": "@Hall"})");
    EXPECT_EQ(Scene().GetParent(Find("Shed")), Find("Hall"));
    EXPECT_NEAR(WorldPosition("Shed").X, shed.X, 1e-4f) << "it stays where it stood";

    EXPECT_EQ(Code(Call("entity.reparent", R"({"subjects": ["@Hall"], "parent": "@Shed"})")), "reparent.cycle");

    Ok("entity.reparent", R"({"subjects": ["@Shed"]})");
    EXPECT_FALSE(Scene().GetParent(Find("Shed")).IsValid()) << "no parent is the top of the level";
    Commands.Undo();
    EXPECT_EQ(Scene().GetParent(Find("Shed")), Find("Hall"));
}

TEST_F(EntityOperationsTest, AnAmbiguousNameRefusesWithItsCandidates)
{
    Ok("entity.rename", R"({"subject": "@Shed", "name": "Door"})");
    const JsonValue answer = Call("entity.delete", R"({"subjects": ["@Door"]})");
    EXPECT_EQ(Code(answer), "subject.ambiguous");
    EXPECT_EQ(answer.Find("detail")->Find("candidates")->AsArray().size(), 2u);
    EXPECT_TRUE(Exists("Hall")) << "a refusal changes nothing";
}
