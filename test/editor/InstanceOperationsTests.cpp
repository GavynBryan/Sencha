// instance.place: a scene source placed as one undo step, its projected
// entities coming and going with the placement, in the level and in a draft.

#include "WorkspaceFixture.h"

#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"
#include "render/DraftPreview.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <render/PointLightComponent.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

namespace
{
class InstanceOperationsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Root = std::filesystem::temp_directory_path()
            / ("sencha_instance_ops_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(Root / "props");
        EditorDocument lamp(Logging);
        const EntityId bulb = lamp.GetScene().CreateEntity(Vec3d{ 0, 1, 0 });
        lamp.GetScene().GetRegistry().Components.AddComponent(bulb, PointLightComponent{});
        ASSERT_TRUE(lamp.SaveAs((Root / "props/lamp.sscene").generic_string()));

        Workspace.ActiveDocument().SetContentRoots({ Root });
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Workspace.Level, Operations), errors));
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(Root, ignored);
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

    std::size_t Entities() { return Scene().GetEntityCount(); }

    std::filesystem::path Root;
    EditorOperations Operations;
};
}

TEST_F(InstanceOperationsTest, APlacementIsOneUndoStepWithEverythingItProjects)
{
    const JsonValue placed = Ok("instance.place", R"({"source": "asset://props/lamp.sscene", "at": [4, 0, 2], "turn": 90})");
    const std::string root = placed.Find("placement")->Find("id")->AsString();
    EXPECT_EQ(Entities(), 2u) << "the placement's root and the bulb";
    const JsonValue described = Ok("entity.describe", R"({"subject": ")" + root + R"("})");
    EXPECT_NEAR(described.Find("entity")->Find("position")->AsArray()[0].AsNumber(), 4.0, 1e-3);
    EXPECT_EQ(placed.Find("created")->AsArray().size(), 1u) << "listed once, as its root";

    Commands.Undo();
    EXPECT_EQ(Entities(), 0u);
    Commands.Redo();
    EXPECT_EQ(Entities(), 2u);
    EXPECT_TRUE(ResolveEntitySubject(Workspace.ActiveDocument(), root).Resolved()) << "redo keeps the placement's ids";

    EXPECT_EQ(Code(Call("instance.place", R"({"source": "asset://props/nope.sscene", "at": [0, 0, 0]})")),
              "instance.refused");
}

TEST_F(InstanceOperationsTest, ADraftedPlacementLandsOnAccept)
{
    Ok("draft.begin", R"({"label": "Light the hall"})");
    Ok("instance.place", R"({"source": "asset://props/lamp.sscene", "at": [1, 0, 1]})");
    EXPECT_EQ(Entities(), 0u) << "only proposed";
    Ok("draft.accept", "{}");
    EXPECT_EQ(Entities(), 2u);
    Commands.Undo();
    EXPECT_EQ(Entities(), 0u);
}

TEST_F(InstanceOperationsTest, ADraftedPlacementIsStagedInThePreview)
{
    Ok("draft.begin", R"({"label": "Light the hall"})");
    Ok("instance.place", R"({"source": "asset://props/lamp.sscene", "at": [1, 0, 1]})");
    const LevelDraft* draft = Workspace.Level.PreviewedDraft();
    ASSERT_NE(draft, nullptr);
    DraftPreview preview{ nullptr, Logging };
    preview.Update(draft, Workspace.Level.BaseOf(*draft), Scene().InteractiveEvaluationPolicy());
    ASSERT_NE(preview.Added(), nullptr);
    EXPECT_EQ(preview.Added()->Scene().GetEntityCount(), 2u) << "the placement projects in the stage";
}
