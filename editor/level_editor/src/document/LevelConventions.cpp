#include "LevelConventions.h"

#include "brush/BrushMesh.h"

#include "project/ProjectFiles.h"

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/assets/AssetRefCodec.h>
#include <core/json/JsonParser.h>
#include <core/metadata/DataSchema.h>
#include <core/metadata/DataSchemaFields.h>

#include <memory>

namespace
{
    DataFieldSchema Length(std::string key, std::string display, std::string summary, double fallback)
    {
        DataFieldSchema field = DataField::Field(std::move(key), DataFieldKind::Float, std::move(display),
                                                 std::move(summary), false);
        field.Default = fallback;
        field.Units = "m";
        field.Numeric.Minimum = 0.01;
        return field;
    }

    DataFieldSchema Material(std::string key, std::string display, std::string summary)
    {
        DataFieldSchema field = DataField::Field(std::move(key), DataFieldKind::AssetRef, std::move(display),
                                                 std::move(summary), false);
        field.Reference.AssetTypeFilter = AssetType::Material;
        return field;
    }

    DataSchema MakeSchema()
    {
        DataSchema schema;
        schema.TypeName = std::string(kLevelConventionsSubtype);
        schema.DisplayName = "Level Conventions";
        schema.Description = "The project's blockout conventions: what generated walls, openings "
                             "and new spaces start from.";
        schema.Root = DataField::Record({}, "Level conventions", {}, {
            Length("shell_thickness", "Wall thickness", "How thick generated walls are.",
                   kDefaultShellThickness),
            Length("opening_width", "Opening width", "A new doorway's width.", kDefaultOpeningWidth),
            Length("opening_height", "Opening height", "A new doorway's height.", kDefaultOpeningHeight),
            Length("space_height", "Space height", "A new space's floor-to-ceiling height.",
                   kDefaultSpaceHeight),
            Material("open_face_material", "Open face material",
                     "A space face painted with this gets no generated wall."),
            Material("wall_material", "Wall material", "What generated walls are painted with."),
            Material("floor_material", "Floor material", "What generated floors are painted with."),
            Material("ceiling_material", "Ceiling material", "What generated ceilings are painted with."),
        });
        return schema;
    }

    std::optional<AssetRef> Reference(const JsonValue& data, std::string_view key)
    {
        const JsonValue* value = data.Find(key);
        return value != nullptr && !value->IsNull() ? AssetRefFromJson(*value) : std::nullopt;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        JsonValue values;
        std::vector<DataValidationError> errors;
        DataValueRules rules;
        rules.FillDefaults = true;
        DataAssetCompileResult result;
        if (!NormalizeDataValue(&data, LevelConventionsSchema().Root, "$", rules, &values, errors))
        {
            result.Error = FormatDataValidationErrors(errors);
            return result;
        }
        result.Value = std::make_shared<LevelConventions>(LevelConventionsFrom(values));
        return result;
    }

    JsonValue Defaults()
    {
        JsonValue values;
        std::vector<DataValidationError> unused;
        DataValueRules rules;
        rules.FillDefaults = true;
        const JsonValue empty(JsonValue::Object{});
        (void)NormalizeDataValue(&empty, LevelConventionsSchema().Root, "$", rules, &values, unused);
        return values;
    }
}

const DataSchema& LevelConventionsSchema()
{
    static const DataSchema schema = MakeSchema();
    return schema;
}

void RegisterLevelConventionsData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    DataAssetTypeRegistration type;
    type.Name = std::string(kLevelConventionsSubtype);
    type.CurrentVersion = 1;
    type.Compile = Compile;
    if (!types.Register(std::move(type)))
        return;
    if (!schemas.Register(LevelConventionsSchema()))
        (void)types.Unregister(kLevelConventionsSubtype);
}

LevelConventionsLookup FindLevelConventions(std::span<const std::filesystem::path> roots)
{
    LevelConventionsLookup lookup;
    lookup.Values = Defaults();

    std::vector<ProjectFile> found;
    for (ProjectFile& file : ScanProjectFiles(roots))
        if (file.Subtype == kLevelConventionsSubtype)
            found.push_back(std::move(file));
    if (found.empty())
        return lookup;
    if (found.size() > 1)
    {
        std::string message = "more than one level.conventions asset:";
        for (const ProjectFile& file : found)
            message += " " + file.AssetPath;
        lookup.Problems.push_back(std::move(message));
        return lookup;
    }

    const std::optional<std::string> text = ReadProjectFileText(found.front().FilePath);
    const std::optional<JsonValue> envelope = text ? JsonParse(*text) : std::nullopt;
    const JsonValue* data = envelope ? envelope->Find("data") : nullptr;
    if (data == nullptr)
    {
        lookup.Problems.push_back(found.front().AssetPath + ": not a data asset with a data object");
        return lookup;
    }
    JsonValue values;
    std::vector<DataValidationError> errors;
    DataValueRules rules;
    rules.FillDefaults = true;
    if (!NormalizeDataValue(data, LevelConventionsSchema().Root, "$", rules, &values, errors))
    {
        lookup.Problems.push_back(found.front().AssetPath + ": " + FormatDataValidationErrors(errors));
        return lookup;
    }
    lookup.AssetPath = found.front().AssetPath;
    lookup.Values = std::move(values);
    return lookup;
}

LevelConventions LevelConventionsFrom(const JsonValue& values)
{
    LevelConventions conventions;
    conventions.ShellThickness = static_cast<float>(values.Find("shell_thickness")->AsNumber());
    conventions.OpeningWidth = static_cast<float>(values.Find("opening_width")->AsNumber());
    conventions.OpeningHeight = static_cast<float>(values.Find("opening_height")->AsNumber());
    conventions.SpaceHeight = static_cast<float>(values.Find("space_height")->AsNumber());
    conventions.OpenFaceMaterial = Reference(values, "open_face_material");
    conventions.WallMaterial = Reference(values, "wall_material");
    conventions.FloorMaterial = Reference(values, "floor_material");
    conventions.CeilingMaterial = Reference(values, "ceiling_material");
    return conventions;
}

void PaintByConvention(BrushMesh& mesh, const LevelConventions& conventions)
{
    for (BrushFace& face : mesh.Faces)
    {
        const float up = BrushComputeFaceNormal(mesh, face).Y;
        const std::optional<AssetRef>& material = up < -0.7f ? conventions.FloorMaterial
            : up > 0.7f                                       ? conventions.CeilingMaterial
                                                              : conventions.WallMaterial;
        if (material)
            face.Material.Material = *material;
    }
}
