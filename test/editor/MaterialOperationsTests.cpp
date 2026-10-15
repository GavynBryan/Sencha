// assets.list and material.apply: the project's materials found as the shelf
// finds them, painted onto every face of a brush or the face toward a direction.

#include "WorkspaceFixture.h"

#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace
{
class MaterialOperationsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Root = std::filesystem::temp_directory_path()
            / ("sencha_material_ops_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        for (const char* path : { "materials/dev/gray.smat", "materials/stone/wall.smat" })
        {
            std::filesystem::create_directories((Root / path).parent_path());
            std::ofstream(Root / path) << "{}";
        }
        Workspace.ActiveDocument().SetContentRoots({ Root });
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Workspace.Level, Operations), errors));
        Ok("brush.box", R"({"min": [0, 0, 0], "max": [1, 1, 1], "name": "Crate"})");
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

    // How many of the crate's faces carry `path`.
    std::size_t Painted(std::string_view path)
    {
        const EntityId crate = ResolveEntitySubject(Workspace.ActiveDocument(), "@Crate").Entities.front();
        std::size_t count = 0;
        for (const BrushFace& face : Scene().TryGetBrushMesh(crate)->Faces)
            count += face.Material.Material.Path == path ? 1 : 0;
        return count;
    }

    std::filesystem::path Root;
    EditorOperations Operations;
};
}

TEST_F(MaterialOperationsTest, TheProjectsMaterialsAreFoundAsTheShelfFindsThem)
{
    EXPECT_EQ(Ok("assets.list", R"({"query": "t:material"})").Find("assets")->AsArray().size(), 2u);
    const JsonValue stone = Ok("assets.list", R"({"query": "t:material stone"})");
    ASSERT_EQ(stone.Find("assets")->AsArray().size(), 1u);
    EXPECT_EQ(stone.Find("assets")->AsArray()[0].Find("path")->AsString(), "asset://materials/stone/wall.smat");
    EXPECT_EQ(stone.Find("assets")->AsArray()[0].Find("kind")->AsString(), "Material");
}

TEST_F(MaterialOperationsTest, AMaterialPaintsEveryFaceOrTheOneTowardADirection)
{
    Ok("material.apply", R"({"subjects": ["@Crate"], "material": "asset://materials/dev/gray.smat"})");
    EXPECT_EQ(Painted("asset://materials/dev/gray.smat"), 6u);
    Ok("material.apply", R"({"subjects": ["@Crate"], "material": "asset://materials/stone/wall.smat", "face": "up"})");
    EXPECT_EQ(Painted("asset://materials/stone/wall.smat"), 1u);
    EXPECT_EQ(Painted("asset://materials/dev/gray.smat"), 5u);

    Commands.Undo();
    EXPECT_EQ(Painted("asset://materials/dev/gray.smat"), 6u);
    EXPECT_EQ(Code(Call("material.apply", R"({"subjects": ["@Crate"], "material": "asset://materials/nope.smat"})")),
              "material.unknown");
}
