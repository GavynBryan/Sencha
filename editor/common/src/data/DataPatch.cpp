#include "data/DataPatch.h"

#include "data/DataDocument.h"
#include "data/DataPath.h"

#include <core/metadata/DataSchema.h>

#include <algorithm>
#include <format>
#include <span>

namespace
{
bool IsKeyedList(const DataFieldSchema& field)
{
    return field.Kind == DataFieldKind::Array && ArrayMergeKey(field) != nullptr;
}

// What a member the patch lacks starts as: empty where a source supplies it, so
// only what is written next is set here; whole where a patch can only replace.
JsonValue Seed(const DataFieldSchema& member, const JsonValue* composed)
{
    if (composed == nullptr)
        return CreateDefaultDataValue(member);
    const DataFieldSchema& reading = *UnwrapDataField(&member);
    if (reading.Kind == DataFieldKind::Record || IsKeyedList(reading))
        return JsonValue(JsonValue::Object{});
    return *composed;
}

const JsonValue* ComposedKeyed(const JsonValue* composed, const DataFieldSchema& list, std::string_view key)
{
    return composed != nullptr ? FindKeyedElement(*composed, list, key) : nullptr;
}

void EraseMember(JsonValue& record, std::string_view key)
{
    std::erase_if(record.AsObject(), [&](const auto& member) { return member.first == key; });
}

void EraseElement(JsonValue& list, const JsonValue& element)
{
    JsonValue::Array& elements = list.AsArray();
    elements.erase(elements.begin() + (&element - elements.data()));
}

bool Erase(JsonValue& node, const DataFieldSchema* field, std::span<const DataPathStep> steps)
{
    const DataPathStep& step = steps.front();
    const DataFieldSchema* reading = UnwrapDataField(field);
    JsonValue* child = nullptr;
    const DataFieldSchema* childField = nullptr;
    switch (step.Kind)
    {
    case DataPathStepKind::Member:
        child = node.IsObject() ? node.Find(step.Key) : nullptr;
        childField = reading != nullptr ? FindChild(*reading, step.Key) : nullptr;
        break;
    case DataPathStepKind::Keyed:
        if (reading == nullptr)
            return false;
        child = node.IsObject() ? node.Find(step.Key) : FindKeyedElement(node, *reading, step.Key);
        childField = DataFieldElement(*reading);
        break;
    case DataPathStepKind::Index:
        child = node.IsArray() && step.Index < node.AsArray().size() ? &node.AsArray()[step.Index] : nullptr;
        childField = reading != nullptr ? DataFieldElement(*reading) : nullptr;
        break;
    }
    if (child == nullptr)
        return false;
    const bool last = steps.size() == 1;
    if (!last && !Erase(*child, childField, steps.subspan(1)))
        return false;
    if (!last && !(child->IsObject() && child->AsObject().empty()))
        return true;
    if (step.Kind == DataPathStepKind::Index)
        return !last;
    if (node.IsObject())
        EraseMember(node, step.Key);
    else
        EraseElement(node, *child);
    return true;
}
} // namespace

DataPatchLocation LocateInPatch(JsonValue& local, const JsonValue* composed, const DataFieldSchema& root,
                                std::string_view path, bool create)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (!steps.has_value())
        return {};
    JsonValue* value = &local;
    const DataFieldSchema* field = &root;
    for (const DataPathStep& step : *steps)
    {
        const DataFieldSchema& reading = *UnwrapDataField(field);
        if (step.Kind == DataPathStepKind::Member)
        {
            const DataFieldSchema* member = FindChild(reading, step.Key);
            if (member == nullptr)
                return {};
            const JsonValue* inherited = composed != nullptr && composed->IsObject() ? composed->Find(step.Key) : nullptr;
            if (!value->IsObject())
            {
                if (!create)
                    return {};
                *value = JsonValue(JsonValue::Object{});
            }
            JsonValue* next = value->Find(step.Key);
            if (next == nullptr)
            {
                if (!create)
                    return {};
                value->AsObject().emplace_back(step.Key, Seed(*member, inherited));
                next = &value->AsObject().back().second;
            }
            value = next;
            composed = inherited;
            field = member;
            continue;
        }
        const DataFieldSchema* element = DataFieldElement(reading);
        if (reading.Kind != DataFieldKind::Array || element == nullptr)
            return {};
        if (step.Kind == DataPathStepKind::Index)
        {
            if (!value->IsArray() || step.Index >= value->AsArray().size())
                return {};
            value = &value->AsArray()[step.Index];
            composed = composed != nullptr && composed->IsArray() && step.Index < composed->AsArray().size()
                ? &composed->AsArray()[step.Index]
                : nullptr;
            field = element;
            continue;
        }
        const JsonValue* inherited = ComposedKeyed(composed, reading, step.Key);
        JsonValue* next = nullptr;
        if (value->IsArray())
            next = FindKeyedElement(*value, reading, step.Key);
        else if (value->IsObject())
        {
            next = value->Find(step.Key);
            if (next != nullptr && next->IsNull())
                return {};
            if (next == nullptr && create && inherited != nullptr)
            {
                value->AsObject().emplace_back(step.Key, JsonValue(JsonValue::Object{}));
                next = &value->AsObject().back().second;
            }
        }
        if (next == nullptr)
            return {};
        value = next;
        composed = inherited;
        field = element;
    }
    return { value, field };
}

bool EraseFromPatch(JsonValue& local, const DataFieldSchema& root, std::string_view path)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    return steps.has_value() && !steps->empty() && Erase(local, &root, *steps);
}

bool RenameKeyedElement(JsonValue& local, const JsonValue& inherited, const DataFieldSchema& root,
                        std::string_view path, std::string_view name)
{
    const std::optional<std::string_view> parent = DataPathParent(path);
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (name.empty() || !parent || !steps || steps->empty() || steps->back().Kind != DataPathStepKind::Keyed
        || FindDataValue(inherited, root, path) != nullptr
        || FindDataValue(inherited, root, std::format("{}{{{}}}", *parent, name)) != nullptr)
        return false;
    const DataPatchLocation list = LocateInPatch(local, nullptr, root, *parent, false);
    const DataFieldSchema* field = list.Field != nullptr ? UnwrapDataField(list.Field) : nullptr;
    const DataFieldSchema* key = field != nullptr ? ArrayMergeKey(*field) : nullptr;
    if (key == nullptr)
        return false;
    const std::string& current = steps->back().Key;
    JsonValue* element = nullptr;
    if (list.Value->IsObject())
    {
        if (list.Value->Find(name) != nullptr)
            return false;
        const auto entry = std::ranges::find(list.Value->AsObject(), current, [](const auto& member) {
            return std::string_view(member.first);
        });
        if (entry == list.Value->AsObject().end())
            return false;
        entry->first = std::string(name);
        element = &entry->second;
    }
    else if (list.Value->IsArray())
    {
        if (FindKeyedElement(*list.Value, *field, name) != nullptr)
            return false;
        element = FindKeyedElement(*list.Value, *field, current);
    }
    if (element == nullptr || !element->IsObject())
        return false;
    if (JsonValue* stated = element->Find(key->Key))
        *stated = JsonValue(std::string(name));
    return true;
}
