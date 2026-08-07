#include <gtest/gtest.h>

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include "PlayerAvatarData.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view kTypeName = "player.avatar";

    // The registry pair the template hands the engine, stood up on its own so a
    // test exercises the same registration path the game module uses.
    struct AvatarTypes
    {
        AvatarTypes() { RegisterPlayerAvatarData(Types, Schemas); }
        ~AvatarTypes() { UnregisterPlayerAvatarData(Types, Schemas); }

        [[nodiscard]] const DataSchema& Schema() const
        {
            return *Schemas.Find(kTypeName);
        }

        [[nodiscard]] DataAssetCompileResult Compile(const JsonValue& data) const
        {
            return Types.Find(kTypeName)->Compile(data);
        }

        DataAssetTypeRegistry Types;
        DataSchemaRegistry Schemas;
    };

    JsonValue ParseData(std::string_view json)
    {
        const std::optional<JsonValue> parsed = JsonParse(json);
        EXPECT_TRUE(parsed.has_value()) << "test document is not valid json";
        return parsed.has_value() ? *parsed : JsonValue{};
    }

    std::string ReadRepoFile(std::string_view relativePath)
    {
        const std::filesystem::path path =
            std::filesystem::path(SENCHA_REPO_ROOT) / relativePath;
        std::ifstream file(path);
        if (!file)
            return {};
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }
}

TEST(PlayerAvatarData, RegistersItsTypeAndSchema)
{
    AvatarTypes avatar;

    ASSERT_NE(avatar.Types.Find(kTypeName), nullptr);
    ASSERT_NE(avatar.Schemas.Find(kTypeName), nullptr);
    EXPECT_EQ(avatar.Schema().TypeName, kTypeName);
}

TEST(PlayerAvatarData, CompilesMeshAndMaterials)
{
    AvatarTypes avatar;
    const JsonValue data = ParseData(R"({
        "mesh": "asset://meshes/dev/cube.smesh",
        "materials": ["asset://materials/dev/gray.smat"]
    })");

    const DataAssetCompileResult result = avatar.Compile(data);

    ASSERT_TRUE(result.IsValid()) << result.Error;
    const auto* compiled =
        static_cast<const CompiledPlayerAvatar*>(result.Value.get());
    ASSERT_NE(compiled, nullptr);
    EXPECT_EQ(compiled->MeshPath, "asset://meshes/dev/cube.smesh");
    ASSERT_EQ(compiled->MaterialPaths.size(), 1u);
    EXPECT_EQ(compiled->MaterialPaths[0], "asset://materials/dev/gray.smat");
}

// The mesh and materials must be declared as dependencies or the preloader has
// no way to know a pawn's body needs to be resident before it spawns.
TEST(PlayerAvatarData, ReportsItsAssetsAsDependencies)
{
    AvatarTypes avatar;
    const JsonValue data = ParseData(R"({
        "mesh": "asset://meshes/dev/cube.smesh",
        "materials": ["asset://materials/dev/gray.smat", "asset://materials/dev/red.smat"]
    })");

    const DataAssetCompileResult result = avatar.Compile(data);

    ASSERT_TRUE(result.IsValid()) << result.Error;
    ASSERT_EQ(result.Dependencies.size(), 3u);
    EXPECT_EQ(result.Dependencies[0].Type, AssetType::StaticMesh);
    EXPECT_EQ(result.Dependencies[0].Path, "asset://meshes/dev/cube.smesh");
    EXPECT_EQ(result.Dependencies[1].Type, AssetType::Material);
    EXPECT_EQ(result.Dependencies[2].Type, AssetType::Material);
}

TEST(PlayerAvatarData, RejectsAMissingMesh)
{
    AvatarTypes avatar;
    const JsonValue data = ParseData(R"({
        "materials": ["asset://materials/dev/gray.smat"]
    })");

    EXPECT_FALSE(avatar.Compile(data).IsValid());
}

TEST(PlayerAvatarData, RejectsAnEmptyMaterialList)
{
    AvatarTypes avatar;
    const JsonValue data = ParseData(R"({
        "mesh": "asset://meshes/dev/cube.smesh",
        "materials": []
    })");

    EXPECT_FALSE(avatar.Compile(data).IsValid())
        << "a mesh with no materials has nothing to draw it with";
}

TEST(PlayerAvatarData, RejectsANonStringMaterialEntry)
{
    AvatarTypes avatar;
    const JsonValue data = ParseData(R"({
        "mesh": "asset://meshes/dev/cube.smesh",
        "materials": [17]
    })");

    EXPECT_FALSE(avatar.Compile(data).IsValid());
}

// A compiler runs inside the editor as well as the runtime, where the document
// may be anything on disk.
TEST(PlayerAvatarData, RejectsANonObjectDocument)
{
    AvatarTypes avatar;

    EXPECT_FALSE(avatar.Compile(ParseData("[]")).IsValid());
}

// The shipped avatar is content, not a fixture: if it stops loading, a project
// made from the template starts with an invisible player.
TEST(PlayerAvatarData, TheTemplatesShippedAvatarValidatesAndCompiles)
{
    AvatarTypes avatar;
    const std::string document =
        ReadRepoFile("template/assets/data/player_avatar.sdata");
    ASSERT_FALSE(document.empty());

    const std::optional<JsonValue> parsed = JsonParse(document);
    ASSERT_TRUE(parsed.has_value());
    const JsonValue* type = parsed->Find("type");
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->AsString(), kTypeName);
    ASSERT_NE(parsed->Find("version"), nullptr);
    const JsonValue* data = parsed->Find("data");
    ASSERT_NE(data, nullptr);

    std::vector<DataValidationError> errors;
    EXPECT_TRUE(ValidateDataAgainstSchema(*data, avatar.Schema(), errors));
    for (const DataValidationError& error : errors)
        ADD_FAILURE() << error.Path << ": " << error.Message;

    EXPECT_TRUE(avatar.Compile(*data).IsValid());
}
