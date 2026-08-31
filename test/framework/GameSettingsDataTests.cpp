#include <gtest/gtest.h>

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include "GameSettingsData.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view kTypeName = "game.settings";

    struct SettingsTypes
    {
        SettingsTypes() { RegisterGameSettingsData(Types, Schemas); }
        ~SettingsTypes() { UnregisterGameSettingsData(Types, Schemas); }

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

TEST(GameSettingsData, RegistersItsTypeAndSchema)
{
    SettingsTypes settings;
    ASSERT_NE(settings.Types.Find(kTypeName), nullptr);
    ASSERT_NE(settings.Schemas.Find(kTypeName), nullptr);
}

TEST(GameSettingsData, CompilesBothScenesWithDependencies)
{
    SettingsTypes settings;
    const DataAssetCompileResult result = settings.Compile(ParseData(R"({
        "player_pawn": "asset://prefabs/player_pawn.smap",
        "turret": "asset://prefabs/turret.smap"
    })"));
    ASSERT_TRUE(result.Error.empty()) << result.Error;
    const auto* compiled =
        static_cast<const CompiledGameSettings*>(result.Value.get());
    ASSERT_NE(compiled, nullptr);
    EXPECT_EQ(compiled->PlayerPawnScenePath, "asset://prefabs/player_pawn.smap");
    EXPECT_EQ(compiled->TurretScenePath, "asset://prefabs/turret.smap");
    ASSERT_EQ(result.Dependencies.size(), 2u);
    EXPECT_EQ(result.Dependencies[0].Type, AssetType::Scene);
}

TEST(GameSettingsData, AbsentFieldsMeanProceduralSpawns)
{
    SettingsTypes settings;
    const DataAssetCompileResult result = settings.Compile(ParseData("{}"));
    ASSERT_TRUE(result.Error.empty()) << result.Error;
    const auto* compiled =
        static_cast<const CompiledGameSettings*>(result.Value.get());
    ASSERT_NE(compiled, nullptr);
    EXPECT_TRUE(compiled->PlayerPawnScenePath.empty());
    EXPECT_TRUE(compiled->TurretScenePath.empty());
    EXPECT_TRUE(result.Dependencies.empty());
}

TEST(GameSettingsData, ANonStringSceneIsRejected)
{
    SettingsTypes settings;
    const DataAssetCompileResult result =
        settings.Compile(ParseData(R"({ "player_pawn": 7 })"));
    EXPECT_FALSE(result.Error.empty());
    EXPECT_EQ(result.Value, nullptr);
}

TEST(GameSettingsData, TheShippedSettingsFileCompiles)
{
    const std::string text = ReadRepoFile("template/assets/data/game.sdata");
    ASSERT_FALSE(text.empty());
    const JsonValue envelope = ParseData(text);
    const JsonValue* type = envelope.Find("type");
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->AsString(), kTypeName);
    const JsonValue* data = envelope.Find("data");
    ASSERT_NE(data, nullptr);

    SettingsTypes settings;
    const DataAssetCompileResult result = settings.Compile(*data);
    EXPECT_TRUE(result.Error.empty()) << result.Error;
    const auto* compiled =
        static_cast<const CompiledGameSettings*>(result.Value.get());
    ASSERT_NE(compiled, nullptr);
    EXPECT_FALSE(compiled->PlayerPawnScenePath.empty());
    EXPECT_FALSE(compiled->TurretScenePath.empty());
}
