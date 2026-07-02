#include "EditorThemeFile.h"

#include "EditorUiStyle.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace
{
// The editable palette, by theme key. One table: adding a palette entry here
// makes it themeable (file load, save, and the preferences UI), nothing else
// to touch.
const EditorThemePaletteEntry kThemeEntries[] = {
    { "window_bg", &EditorUi::WindowBg },
    { "panel_bg", &EditorUi::PanelBg },
    { "header_bg", &EditorUi::HeaderBg },
    { "frame_bg", &EditorUi::FrameBg },
    { "frame_bg_hovered", &EditorUi::FrameBgHovered },
    { "frame_bg_active", &EditorUi::FrameBgActive },
    { "border", &EditorUi::Border },
    { "accent", &EditorUi::Accent },
    { "accent_hover", &EditorUi::AccentHover },
    { "accent_dim", &EditorUi::AccentDim },
    { "selected", &EditorUi::Selected },
    { "secondary", &EditorUi::Secondary },
    { "secondary_hover", &EditorUi::SecondaryHover },
    { "button_bg", &EditorUi::ButtonBg },
    { "button_hovered", &EditorUi::ButtonHovered },
    { "warning", &EditorUi::Warning },
    { "danger", &EditorUi::Danger },
    { "critical", &EditorUi::Critical },
    { "success", &EditorUi::Success },
    { "text_primary", &EditorUi::TextPrimary },
    { "text_dim", &EditorUi::TextDim },
};

// The pristine palette, captured before the first theme load or reset so
// switching themes at runtime starts from the built-in defaults, not from
// whatever the previous theme left behind.
std::array<ImVec4, std::size(kThemeEntries)> kBuiltInPalette;
bool kBuiltInCaptured = false;

void CaptureBuiltInPalette()
{
    if (kBuiltInCaptured)
        return;
    for (std::size_t i = 0; i < std::size(kThemeEntries); ++i)
        kBuiltInPalette[i] = *kThemeEntries[i].Color;
    kBuiltInCaptured = true;
}

// Linear -> sRGB (inverse of EditorUi::detail::ToLinear), for re-encoding the
// palette to the authored hex form on save.
float ToSrgb(float linear)
{
    return linear <= 0.0031308f ? linear * 12.92f
                                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

unsigned SrgbByte(float linear)
{
    const float s = ToSrgb(linear < 0.0f ? 0.0f : (linear > 1.0f ? 1.0f : linear));
    return static_cast<unsigned>(s * 255.0f + 0.5f);
}

bool HexNibble(char c, unsigned& out)
{
    if (c >= '0' && c <= '9') { out = static_cast<unsigned>(c - '0'); return true; }
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower >= 'a' && lower <= 'f') { out = static_cast<unsigned>(lower - 'a' + 10); return true; }
    return false;
}

bool HexByte(const std::string& s, std::size_t offset, float& out)
{
    unsigned hi = 0;
    unsigned lo = 0;
    if (!HexNibble(s[offset], hi) || !HexNibble(s[offset + 1], lo))
        return false;
    out = static_cast<float>(hi * 16 + lo) / 255.0f;
    return true;
}
}

bool ParseThemeColor(const std::string& hex, float& r, float& g, float& b, float& a)
{
    if (hex.size() != 7 && hex.size() != 9)
        return false;
    if (hex[0] != '#')
        return false;

    float sr = 0.0f;
    float sg = 0.0f;
    float sb = 0.0f;
    float sa = 1.0f;
    if (!HexByte(hex, 1, sr) || !HexByte(hex, 3, sg) || !HexByte(hex, 5, sb))
        return false;
    if (hex.size() == 9 && !HexByte(hex, 7, sa))
        return false;

    // Authored sRGB -> linear (the swapchain encodes on write); alpha is linear
    // already.
    r = EditorUi::detail::ToLinear(sr);
    g = EditorUi::detail::ToLinear(sg);
    b = EditorUi::detail::ToLinear(sb);
    a = sa;
    return true;
}

std::span<const EditorThemePaletteEntry> EditorThemePalette()
{
    CaptureBuiltInPalette();
    return kThemeEntries;
}

void ResetEditorThemePalette()
{
    CaptureBuiltInPalette();
    for (std::size_t i = 0; i < std::size(kThemeEntries); ++i)
        *kThemeEntries[i].Color = kBuiltInPalette[i];
}

bool SaveEditorTheme(const std::filesystem::path& path, std::string* error)
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        if (error != nullptr)
            *error = "cannot write theme '" + path.string() + "'";
        return false;
    }

    file << "{\n  \"colors\": {\n";
    for (std::size_t i = 0; i < std::size(kThemeEntries); ++i)
    {
        const ImVec4& c = *kThemeEntries[i].Color;
        char hex[10];
        if (c.w >= 1.0f)
            std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", SrgbByte(c.x), SrgbByte(c.y), SrgbByte(c.z));
        else
            std::snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X", SrgbByte(c.x), SrgbByte(c.y), SrgbByte(c.z),
                          static_cast<unsigned>(c.w * 255.0f + 0.5f));
        file << "    \"" << kThemeEntries[i].Key << "\": \"" << hex << '"'
             << (i + 1 < std::size(kThemeEntries) ? ",\n" : "\n");
    }
    file << "  }\n}\n";

    if (!file.good())
    {
        if (error != nullptr)
            *error = "write failed for theme '" + path.string() + "'";
        return false;
    }
    return true;
}

bool LoadEditorTheme(const std::filesystem::path& path, std::string* error)
{
    CaptureBuiltInPalette();
    std::ifstream file(path);
    if (!file.is_open())
    {
        if (error != nullptr)
            *error = "cannot open theme '" + path.string() + "'";
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> root = JsonParse(buffer.str(), &parseError);
    if (!root.has_value())
    {
        if (error != nullptr)
            *error = "theme '" + path.string() + "': " + parseError.Message;
        return false;
    }

    const JsonValue* colors = root->Find("colors");
    if (colors == nullptr || !colors->IsObject())
    {
        if (error != nullptr)
            *error = "theme '" + path.string() + "' has no \"colors\" object";
        return false;
    }

    // A theme file describes the full look: keys it omits fall back to the
    // built-in default, not to whatever the previously loaded theme set.
    ResetEditorThemePalette();

    std::string problems;
    for (const auto& [key, value] : colors->AsObject())
    {
        ImVec4* target = nullptr;
        for (const EditorThemePaletteEntry& entry : kThemeEntries)
            if (key == entry.Key)
            {
                target = entry.Color;
                break;
            }
        if (target == nullptr)
        {
            problems += " unknown key '" + key + "';";
            continue;
        }
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
        if (!value.IsString() || !ParseThemeColor(value.AsString(), r, g, b, a))
        {
            problems += " bad color for '" + key + "';";
            continue;
        }
        *target = ImVec4(r, g, b, a);
    }

    if (!problems.empty() && error != nullptr)
        *error = "theme '" + path.string() + "':" + problems;
    return true;
}
