#include "OperationArguments.h"

#include "scene_source/Json5Convert.h"
#include "scene_source/Json5Parser.h"

#include <vector>

namespace
{
    std::optional<JsonValue> ParseJson5(std::string_view text)
    {
        const std::optional<Json5Value> parsed = Json5Parse(text);
        if (!parsed)
            return std::nullopt;
        return Json5ToJson(*parsed);
    }

    bool IsSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    std::string_view Trim(std::string_view text)
    {
        while (!text.empty() && IsSpace(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && IsSpace(text.back()))
            text.remove_suffix(1);
        return text;
    }

    // Whitespace separates pairs only outside quotes and brackets, so a value
    // may be an array, an object or a quoted string with spaces in it. False
    // when a quote or bracket is left open.
    bool SplitPairs(std::string_view text, std::vector<std::string_view>& pairs)
    {
        int depth = 0;
        char quote = 0;
        std::size_t start = std::string_view::npos;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (start == std::string_view::npos)
            {
                if (IsSpace(c))
                    continue;
                start = i;
            }
            if (quote != 0)
            {
                if (c == '\\')
                    ++i;
                else if (c == quote)
                    quote = 0;
                continue;
            }
            if (c == '"' || c == '\'')
                quote = c;
            else if (c == '[' || c == '{')
                ++depth;
            else if (c == ']' || c == '}')
                --depth;
            else if (IsSpace(c) && depth == 0)
            {
                pairs.push_back(text.substr(start, i - start));
                start = std::string_view::npos;
            }
        }
        if (start != std::string_view::npos)
            pairs.push_back(text.substr(start));
        return quote == 0 && depth == 0;
    }
}

std::optional<JsonValue> OperationArgumentsFromText(std::string_view text, std::string& error)
{
    text = Trim(text);
    if (text.empty())
        return JsonValue(JsonValue::Object{});

    if (text.front() == '{')
    {
        std::optional<JsonValue> object = ParseJson5(text);
        if (!object || !object->IsObject())
        {
            error = "the argument object is not valid JSON5";
            return std::nullopt;
        }
        return object;
    }

    std::vector<std::string_view> pairs;
    if (!SplitPairs(text, pairs))
    {
        error = "a quote or bracket is left open";
        return std::nullopt;
    }
    JsonValue::Object members;
    for (const std::string_view pair : pairs)
    {
        const std::size_t equals = pair.find('=');
        if (equals == std::string_view::npos || equals == 0)
        {
            error = "'" + std::string(pair) + "' is not name=value";
            return std::nullopt;
        }
        const std::string name(pair.substr(0, equals));
        for (const auto& [existing, unused] : members)
        {
            (void)unused;
            if (existing == name)
            {
                error = "'" + name + "' is given twice";
                return std::nullopt;
            }
        }
        const std::string_view valueText = pair.substr(equals + 1);
        std::optional<JsonValue> value = ParseJson5(valueText);
        members.emplace_back(name, value ? std::move(*value) : JsonValue(std::string(valueText)));
    }
    return JsonValue(std::move(members));
}
