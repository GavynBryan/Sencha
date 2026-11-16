#include "scene_source/SceneComponentDiff.h"

#include <core/metadata/DataSchema.h>

#include <optional>

namespace
{
const Json5Value* KeyOf(const Json5Value& element, const DataFieldSchema& key)
{
    const Json5Value* stated = element.IsObject() ? element.Find(key.Key) : nullptr;
    return stated != nullptr && stated->IsString() ? stated : nullptr;
}

const Json5Value* ElementKeyed(const Json5Value& list, const DataFieldSchema& key, const std::string& name)
{
    for (const Json5Value& element : list.Elements)
        if (const Json5Value* stated = KeyOf(element, key); stated != nullptr && stated->Text == name)
            return &element;
    return nullptr;
}

std::optional<Json5Value> DiffValue(const Json5Value& live, const Json5Value& baseline, const DataFieldSchema* node);

std::optional<Json5Value> DiffKeyed(const Json5Value& live, const Json5Value& baseline, const DataFieldSchema& list,
                                    const DataFieldSchema& key)
{
    const DataFieldSchema* element = DataFieldElement(list);
    Json5Value patch = Json5Value::MakeObject();
    for (const Json5Value& item : live.Elements)
    {
        const Json5Value* name = KeyOf(item, key);
        if (name == nullptr)
            return live;
        if (const Json5Value* was = ElementKeyed(baseline, key, name->Text))
        {
            if (std::optional<Json5Value> changed = DiffValue(item, *was, element))
                patch.Members.emplace_back(name->Text, std::move(*changed));
        }
        else
            patch.Members.emplace_back(name->Text, item);
    }
    for (const Json5Value& item : baseline.Elements)
        if (const Json5Value* name = KeyOf(item, key); name != nullptr && ElementKeyed(live, key, name->Text) == nullptr)
            patch.Members.emplace_back(name->Text, Json5Value{});
    return patch.Members.empty() ? std::nullopt : std::optional<Json5Value>(std::move(patch));
}

std::optional<Json5Value> DiffValue(const Json5Value& live, const Json5Value& baseline, const DataFieldSchema* node)
{
    node = UnwrapDataField(node);
    if (node != nullptr && node->Kind == DataFieldKind::Array && live.IsArray() && baseline.IsArray())
        if (const DataFieldSchema* key = ArrayMergeKey(*node))
            return DiffKeyed(live, baseline, *node, *key);
    if (live.IsObject() && baseline.IsObject())
    {
        Json5Value patch = Json5Value::MakeObject();
        for (const Json5Value::Member& member : live.Members)
        {
            const Json5Value* was = baseline.Find(member.first);
            if (was == nullptr)
            {
                patch.Members.push_back(member);
                continue;
            }
            const DataFieldSchema* child =
                node != nullptr && node->Kind == DataFieldKind::Record ? FindChild(*node, member.first) : nullptr;
            if (std::optional<Json5Value> changed = DiffValue(member.second, *was, child))
                patch.Members.emplace_back(member.first, std::move(*changed));
        }
        for (const Json5Value::Member& member : baseline.Members)
            if (live.Find(member.first) == nullptr)
                patch.Members.emplace_back(member.first, Json5Value{});
        return patch.Members.empty() ? std::nullopt : std::optional<Json5Value>(std::move(patch));
    }
    return live.SameValueAs(baseline) ? std::nullopt : std::optional<Json5Value>(live);
}
} // namespace

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
        if (std::optional<Json5Value> changed = DiffValue(member.second, *was, schema))
            diff.Patch.Members.emplace_back(member.first, std::move(*changed));
    }
    for (const Json5Value::Member& member : baseline.Members)
        if (!IsDocumentLocalComponent(member.first) && live.Find(member.first) == nullptr)
            diff.Removed.push_back(member.first);
    return diff;
}
