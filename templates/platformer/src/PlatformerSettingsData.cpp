#include "PlatformerSettingsData.h"

#include <core/json/JsonValue.h>

#include <memory>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::string_view kTypeName = "game.settings";

    DataFieldSchema MakeSceneField(const char* key, const char* display,
                                   const char* summary)
    {
        DataFieldSchema field;
        field.Key = key;
        field.DisplayName = display;
        field.Summary = summary;
        field.Kind = DataFieldKind::AssetRef;
        field.Reference.AssetTypeFilter = AssetType::Scene;
        return field;
    }

    DataSchema MakeGameSettingsSchema()
    {
        DataFieldSchema root;
        root.Kind = DataFieldKind::Record;
        root.Children.push_back(MakeSceneField(
            "player_pawn", "Player pawn",
            "Cooked scene spawned as a participant's body."));

        DataSchema schema;
        schema.TypeName = std::string(kTypeName);
        schema.DisplayName = "Game settings";
        schema.Description =
            "Game-wide choices: which prefabs stand in for the built-in "
            "archetypes.";
        schema.Root = std::move(root);
        return schema;
    }

    DataAssetCompileResult CompileGameSettings(const JsonValue& data)
    {
        DataAssetCompileResult result;
        if (!data.IsObject())
        {
            result.Error = "game settings data must be an object";
            return result;
        }

        auto settings = std::make_shared<CompiledPlatformerSettings>();
        const auto readScene = [&](const char* key, std::string& out) -> bool
        {
            const JsonValue* value = data.Find(key);
            if (value == nullptr)
                return true; // absent = procedural spawn
            if (!value->IsString())
            {
                result.Error =
                    std::string("game settings '") + key + "' must be an "
                    "asset path string";
                return false;
            }
            out = value->AsString();
            if (!out.empty())
                result.Dependencies.push_back(AssetRef{ AssetType::Scene, out });
            return true;
        };

        if (!readScene("player_pawn", settings->PlayerPawnScenePath))
            return result;

        result.Value = std::move(settings);
        return result;
    }
}

void RegisterPlatformerSettingsData(DataAssetTypeRegistry& types,
                              DataSchemaRegistry& schemas)
{
    DataAssetTypeRegistration type;
    type.Name = std::string(kTypeName);
    type.CurrentVersion = 1;
    type.Compile = CompileGameSettings;
    if (!types.Register(std::move(type)))
        return;

    if (!schemas.Register(MakeGameSettingsSchema()))
        (void)types.Unregister(kTypeName);
}

void UnregisterPlatformerSettingsData(DataAssetTypeRegistry& types,
                                DataSchemaRegistry& schemas)
{
    if (types.Unregister(kTypeName))
        (void)schemas.Unregister(kTypeName);
}
