#include "PlayerAvatarData.h"

#include <core/json/JsonValue.h>

#include <format>
#include <memory>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::string_view kTypeName = "player.avatar";

    DataSchema MakePlayerAvatarSchema()
    {
        DataFieldSchema mesh;
        mesh.Key = "mesh";
        mesh.DisplayName = "Mesh";
        mesh.Summary = "Mesh drawn where the player is.";
        mesh.Kind = DataFieldKind::AssetRef;
        mesh.Reference.AssetTypeFilter = AssetType::StaticMesh;
        mesh.Required = true;

        DataFieldSchema material;
        material.DisplayName = "Material";
        material.Kind = DataFieldKind::AssetRef;
        material.Reference.AssetTypeFilter = AssetType::Material;

        DataFieldSchema materials;
        materials.Key = "materials";
        materials.DisplayName = "Materials";
        materials.Summary =
            "One material per mesh section, in section order.";
        materials.Kind = DataFieldKind::Array;
        materials.Required = true;
        materials.Children.push_back(std::move(material));

        DataFieldSchema root;
        root.Kind = DataFieldKind::Record;
        root.Children.push_back(std::move(mesh));
        root.Children.push_back(std::move(materials));

        DataSchema schema;
        schema.TypeName = std::string(kTypeName);
        schema.DisplayName = "Player avatar";
        schema.Description =
            "The mesh and materials a player body is drawn with.";
        schema.Root = std::move(root);
        return schema;
    }

    DataAssetCompileResult CompilePlayerAvatar(const JsonValue& data)
    {
        DataAssetCompileResult result;
        if (!data.IsObject())
        {
            result.Error = "player avatar data must be an object";
            return result;
        }

        const JsonValue* mesh = data.Find("mesh");
        if (mesh == nullptr || !mesh->IsString())
        {
            result.Error = "player avatar requires a mesh path";
            return result;
        }

        const JsonValue* materials = data.Find("materials");
        if (materials == nullptr || !materials->IsArray())
        {
            result.Error = "player avatar requires a materials array";
            return result;
        }

        auto avatar = std::make_shared<CompiledPlayerAvatar>();
        avatar->MeshPath = mesh->AsString();
        result.Dependencies.push_back(
            AssetRef{ AssetType::StaticMesh, avatar->MeshPath });

        const std::vector<JsonValue>& entries = materials->AsArray();
        avatar->MaterialPaths.reserve(entries.size());
        for (size_t index = 0; index < entries.size(); ++index)
        {
            if (!entries[index].IsString())
            {
                result.Error = std::format(
                    "$.data.materials[{}] must be an asset path string", index);
                return result;
            }
            avatar->MaterialPaths.push_back(entries[index].AsString());
            result.Dependencies.push_back(
                AssetRef{ AssetType::Material, avatar->MaterialPaths.back() });
        }

        if (avatar->MaterialPaths.empty())
        {
            result.Error = "player avatar requires at least one material";
            return result;
        }

        result.Value = std::move(avatar);
        return result;
    }
}

void RegisterPlayerAvatarData(DataAssetTypeRegistry& types,
                              DataSchemaRegistry& schemas)
{
    DataAssetTypeRegistration type;
    type.Name = std::string(kTypeName);
    type.CurrentVersion = 1;
    type.Compile = CompilePlayerAvatar;
    if (!types.Register(std::move(type)))
        return;

    if (!schemas.Register(MakePlayerAvatarSchema()))
        (void)types.Unregister(kTypeName);
}

void UnregisterPlayerAvatarData(DataAssetTypeRegistry& types,
                                DataSchemaRegistry& schemas)
{
    if (types.Unregister(kTypeName))
        (void)schemas.Unregister(kTypeName);
}
