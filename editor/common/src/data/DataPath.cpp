#include "data/DataPath.h"

#include <core/metadata/DataSchema.h>

#include <charconv>

std::optional<std::vector<DataPathStep>> ParseDataPath(std::string_view path)
{
    if (!path.starts_with(kDataPathRoot))
        return std::nullopt;
    path.remove_prefix(kDataPathRoot.size());
    std::vector<DataPathStep> steps;
    while (!path.empty())
    {
        if (path.front() == '.')
        {
            path.remove_prefix(1);
            const std::size_t end = path.find_first_of(".[{");
            steps.push_back({ DataPathStepKind::Member, std::string(path.substr(0, end)) });
            path.remove_prefix(end == std::string_view::npos ? path.size() : end);
        }
        else if (path.front() == '[')
        {
            std::size_t index = 0;
            const auto parsed = std::from_chars(path.data() + 1, path.data() + path.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr == path.data() + path.size() || *parsed.ptr != ']')
                return std::nullopt;
            steps.push_back({ DataPathStepKind::Index, {}, index });
            path.remove_prefix(static_cast<std::size_t>(parsed.ptr - path.data()) + 1);
        }
        else if (path.front() == '{')
        {
            const std::size_t end = path.find('}');
            if (end == std::string_view::npos || end == 1)
                return std::nullopt;
            steps.push_back({ DataPathStepKind::Keyed, std::string(path.substr(1, end - 1)) });
            path.remove_prefix(end + 1);
        }
        else
            return std::nullopt;
    }
    return steps;
}

std::optional<std::string_view> DataPathParent(std::string_view path)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (!steps.has_value() || steps->empty())
        return std::nullopt;
    switch (steps->back().Kind)
    {
    case DataPathStepKind::Index: return path.substr(0, path.rfind('['));
    case DataPathStepKind::Keyed: return path.substr(0, path.rfind('{'));
    default:                      return path.substr(0, path.rfind('.'));
    }
}

std::optional<std::size_t> DataPathLastIndex(std::string_view path)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (!steps.has_value() || steps->empty() || steps->back().Kind != DataPathStepKind::Index)
        return std::nullopt;
    return steps->back().Index;
}

namespace
{
const JsonValue* Find(const JsonValue& data, const DataFieldSchema* root, std::string_view path)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (!steps.has_value())
        return nullptr;
    const JsonValue* value = &data;
    const DataFieldSchema* field = root;
    for (const DataPathStep& step : *steps)
    {
        const DataFieldSchema* reading = UnwrapDataField(field);
        switch (step.Kind)
        {
        case DataPathStepKind::Index:
            if (!value->IsArray() || step.Index >= value->AsArray().size())
                return nullptr;
            value = &value->AsArray()[step.Index];
            field = reading != nullptr ? DataFieldElement(*reading) : nullptr;
            break;
        case DataPathStepKind::Keyed:
            if (reading == nullptr || (value = FindKeyedElement(*value, *reading, step.Key)) == nullptr)
                return nullptr;
            field = DataFieldElement(*reading);
            break;
        case DataPathStepKind::Member:
            if ((value = value->IsObject() ? value->Find(step.Key) : nullptr) == nullptr)
                return nullptr;
            field = reading != nullptr ? FindChild(*reading, step.Key) : nullptr;
            break;
        }
    }
    return value;
}
} // namespace

const JsonValue* FindDataValue(const JsonValue& data, std::string_view path)
{
    return Find(data, nullptr, path);
}

JsonValue* FindDataValue(JsonValue& data, std::string_view path)
{
    return const_cast<JsonValue*>(Find(data, nullptr, path));
}

const JsonValue* FindDataValue(const JsonValue& data, const DataFieldSchema& root, std::string_view path)
{
    return Find(data, &root, path);
}

const JsonValue* FindKeyedElement(const JsonValue& list, const DataFieldSchema& field, std::string_view key)
{
    const DataFieldSchema* keyField = ArrayMergeKey(field);
    if (keyField == nullptr || !list.IsArray())
        return nullptr;
    for (const JsonValue& element : list.AsArray())
        if (const JsonValue* stated = element.IsObject() ? element.Find(keyField->Key) : nullptr;
            stated != nullptr && stated->IsString() && stated->AsString() == key)
            return &element;
    return nullptr;
}

JsonValue* FindKeyedElement(JsonValue& list, const DataFieldSchema& field, std::string_view key)
{
    return const_cast<JsonValue*>(FindKeyedElement(static_cast<const JsonValue&>(list), field, key));
}
