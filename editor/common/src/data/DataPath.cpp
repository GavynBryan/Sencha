#include "data/DataPath.h"

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
            const std::size_t end = path.find_first_of(".[");
            steps.push_back({ std::string(path.substr(0, end)), std::nullopt });
            path.remove_prefix(end == std::string_view::npos ? path.size() : end);
        }
        else if (path.front() == '[')
        {
            std::size_t index = 0;
            const auto parsed = std::from_chars(path.data() + 1, path.data() + path.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr == path.data() + path.size() || *parsed.ptr != ']')
                return std::nullopt;
            steps.push_back({ {}, index });
            path.remove_prefix(static_cast<std::size_t>(parsed.ptr - path.data()) + 1);
        }
        else
            return std::nullopt;
    }
    return steps;
}

const JsonValue* FindDataValue(const JsonValue& data, std::string_view path)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (!steps.has_value())
        return nullptr;
    const JsonValue* value = &data;
    for (const DataPathStep& step : *steps)
    {
        if (step.Index.has_value())
        {
            if (!value->IsArray() || *step.Index >= value->AsArray().size())
                return nullptr;
            value = &value->AsArray()[*step.Index];
        }
        else if ((value = value->IsObject() ? value->Find(step.Key) : nullptr) == nullptr)
            return nullptr;
    }
    return value;
}

JsonValue* FindDataValue(JsonValue& data, std::string_view path)
{
    return const_cast<JsonValue*>(FindDataValue(static_cast<const JsonValue&>(data), path));
}
