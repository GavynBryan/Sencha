#include "scene_source/SceneComponentDiff.h"

#include "scene_source/Json5Composition.h"

#include <core/composition/CompositionPatch.h>
#include <core/metadata/DataSchema.h>

#include <optional>

bool IsDocumentLocalComponent(std::string_view component)
{
    return component == "brush" || component == "baked_brush";
}

SceneComponentDiff DiffSceneComponents(const Json5Value& live, const Json5Value& baseline,
                                       const SceneComponentSchemaFn& schemaOf)
{
    SceneComponentDiff diff;
    for (const Json5Value::Member& member : live.Members)
    {
        if (IsDocumentLocalComponent(member.first))
            continue;
        const Json5Value* was = baseline.Find(member.first);
        if (was == nullptr)
        {
            diff.Added.Members.push_back(member);
            continue;
        }
        const DataFieldSchema* schema = schemaOf ? schemaOf(member.first) : nullptr;
        if (std::optional<Json5Value> changed = PatchFrom(*was, member.second, schema))
            diff.Patch.Members.emplace_back(member.first, std::move(*changed));
    }
    for (const Json5Value::Member& member : baseline.Members)
        if (!IsDocumentLocalComponent(member.first) && live.Find(member.first) == nullptr)
            diff.Removed.push_back(member.first);
    return diff;
}
