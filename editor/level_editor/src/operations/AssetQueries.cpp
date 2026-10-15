#include "AssetQueries.h"

#include "LevelTarget.h"

#include "document/EditorDocument.h"
#include "project/AssetKinds.h"
#include "project/AssetQuery.h"
#include "project/ProjectFiles.h"

#include <core/metadata/DataSchemaFields.h>

#include <string>

namespace
{
    EditorOperationEntry List(const LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "assets.list";
        entry.Definition.Category = "Assets";
        entry.Definition.Description =
            "The project's content files a query finds, best match first, as the asset shelf finds them: words "
            "a name or path must hold, 't:<kind>' for a kind ('t:material', 't:scene') and 'in:<folder>'.";
        DataFieldSchema query = DataField::Field("query", DataFieldKind::String, {}, "what to look for; everything when empty", false);
        query.Default = std::string{};
        DataFieldSchema limit = DataField::Field("limit", DataFieldKind::Int, {}, "most files to list", false);
        limit.Default = std::int64_t{ 50 };
        limit.Numeric.Minimum = 0.0;
        entry.Definition.Parameters = DataField::Record({}, {}, {}, { std::move(query), std::move(limit) });
        entry.Definition.Result = DataField::Record({}, {}, {}, {
            DataField::ArrayOf("assets", {}, {},
                DataField::Record({}, {}, {}, {
                    DataField::Field("path", DataFieldKind::String, {}, "the asset path operations take"),
                    DataField::Field("kind", DataFieldKind::String, {}, "what one of it is called"),
                }),
                true),
            DataField::Field("truncated", DataFieldKind::Bool, {}, {}),
        });
        entry.Execute = [&level](const JsonValue& arguments) {
            const std::vector<ProjectFile> files = ScanProjectFiles(level.Document().ContentRoots());
            const std::vector<const ProjectFile*> found =
                FindAssets(files, AssetQuery{ .Text = arguments.Find("query")->AsString() });
            const std::size_t most = static_cast<std::size_t>(arguments.Find("limit")->AsNumber());
            JsonValue::Array assets;
            for (std::size_t index = 0; index < found.size() && index < most; ++index)
                assets.push_back(JsonValue(JsonValue::Object{
                    { "path", JsonValue(found[index]->AssetPath) },
                    { "kind", JsonValue(KindOf(*found[index]).Label) },
                }));
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "assets", JsonValue(std::move(assets)) },
                { "truncated", JsonValue(found.size() > most) },
            }));
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> AssetQueries(const LevelTarget& level)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(List(level));
    return entries;
}
