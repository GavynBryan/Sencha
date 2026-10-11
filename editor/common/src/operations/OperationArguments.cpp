#include "OperationArguments.h"

#include "scene_source/Json5Convert.h"
#include "scene_source/Json5Parser.h"

#include <string_view>

namespace
{
    std::optional<JsonValue> ParseJson5(std::string_view text)
    {
        const std::optional<Json5Value> parsed = Json5Parse(text);
        if (!parsed)
            return std::nullopt;
        return Json5ToJson(*parsed);
    }
}

std::optional<JsonValue> OperationArgumentsFromTokens(std::span<const std::string> tokens,
                                                      std::string& error)
{
    if (tokens.empty())
        return JsonValue(JsonValue::Object{});

    if (tokens.size() == 1 && tokens.front().starts_with('{'))
    {
        std::optional<JsonValue> object = ParseJson5(tokens.front());
        if (!object || !object->IsObject())
        {
            error = "the argument object is not valid JSON5";
            return std::nullopt;
        }
        return object;
    }

    JsonValue::Object members;
    for (const std::string& token : tokens)
    {
        const std::size_t equals = token.find('=');
        if (equals == std::string::npos || equals == 0)
        {
            error = "'" + token + "' is not name=value";
            return std::nullopt;
        }
        const std::string name = token.substr(0, equals);
        for (const auto& [existing, unused] : members)
        {
            (void)unused;
            if (existing == name)
            {
                error = "'" + name + "' is given twice";
                return std::nullopt;
            }
        }
        const std::string_view text = std::string_view(token).substr(equals + 1);
        std::optional<JsonValue> value = ParseJson5(text);
        members.emplace_back(name, value ? std::move(*value) : JsonValue(std::string(text)));
    }
    return JsonValue(std::move(members));
}
