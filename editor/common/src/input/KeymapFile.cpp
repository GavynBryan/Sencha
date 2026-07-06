#include "KeymapFile.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace
{
std::string LowerCopy(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
}

std::optional<KeyChord> ParseKeyChord(std::string_view text)
{
    KeyChord chord;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t plus = text.find('+', start);
        const std::string_view token =
            text.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start);
        const bool isLast = plus == std::string_view::npos;

        if (token.empty())
            return std::nullopt;

        const std::string lower = LowerCopy(token);
        if (!isLast)
        {
            if (lower == "ctrl")
                chord.Mods.Ctrl = true;
            else if (lower == "shift")
                chord.Mods.Shift = true;
            else if (lower == "alt")
                chord.Mods.Alt = true;
            else
                return std::nullopt; // only the final token may be a key
        }
        else
        {
            chord.Key = SDL_GetKeyFromName(std::string(token).c_str());
            if (chord.Key == SDLK_UNKNOWN)
                return std::nullopt;
            return chord;
        }
        start = plus + 1;
    }
    return std::nullopt;
}

std::unordered_map<std::string, KeyChord> LoadKeymapOverrides(const std::filesystem::path& path,
                                                              std::string* error)
{
    std::unordered_map<std::string, KeyChord> overrides;

    std::ifstream file(path);
    if (!file.is_open())
        return overrides; // no keymap file: defaults apply

    std::stringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> root = JsonParse(buffer.str(), &parseError);
    if (!root.has_value() || !root->IsObject())
    {
        if (error != nullptr)
            *error = "keymap '" + path.string() + "': "
                   + (root.has_value() ? "expected a JSON object" : parseError.Message);
        return overrides;
    }

    std::string problems;
    for (const auto& [action, value] : root->AsObject())
    {
        const std::optional<KeyChord> chord =
            value.IsString() ? ParseKeyChord(value.AsString()) : std::nullopt;
        if (!chord.has_value())
        {
            problems += " bad chord for '" + action + "';";
            continue;
        }
        overrides[action] = *chord;
    }
    if (!problems.empty() && error != nullptr)
        *error = "keymap '" + path.string() + "':" + problems;
    return overrides;
}
