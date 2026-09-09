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
    { "selected_outline", &EditorUi::SelectedOutline },
    { "control_hover", &EditorUi::ControlHover },
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
    { "chassis_bg", &EditorUi::ChassisBg },
    { "metal_base", &EditorUi::MetalBase },
    { "metal_highlight", &EditorUi::MetalHighlight },
    { "metal_shadow", &EditorUi::MetalShadow },
};

// The editable chrome metrics, same shape as the palette table.
const EditorThemeMetricEntry kThemeMetricEntries[] = {
    { "chamfer", &EditorUi::Metrics.Chamfer },
    { "border", &EditorUi::Metrics.Border },
    { "recess", &EditorUi::Metrics.Recess },
    { "rail_height", &EditorUi::Metrics.RailHeight },
    { "header_height", &EditorUi::Metrics.HeaderHeight },
    { "edge_width", &EditorUi::Metrics.EdgeWidth },
    { "glow_alpha", &EditorUi::Metrics.GlowAlpha },
    { "glow_width", &EditorUi::Metrics.GlowWidth },
    { "tracking", &EditorUi::Metrics.Tracking },
    { "ornament_medium_min", &EditorUi::Metrics.OrnamentMediumMin },
    { "ornament_large_min", &EditorUi::Metrics.OrnamentLargeMin },
    { "module_pad", &EditorUi::Metrics.ModulePad },
    { "screw_radius", &EditorUi::Metrics.ScrewRadius },
    { "vent_length", &EditorUi::Metrics.VentLength },
    { "stripe_length", &EditorUi::Metrics.StripeLength },
    { "chassis_border", &EditorUi::Metrics.ChassisBorder },
    { "chassis_chamfer", &EditorUi::Metrics.ChassisChamfer },
};

const EditorThemeDecorEntry kThemeDecorEntries[] = {
    { "hierarchy_empty", &EditorUi::Decor.HierarchyEmpty },
    { "material_browser_empty", &EditorUi::Decor.MaterialBrowserEmpty },
    { "scene_browser_empty", &EditorUi::Decor.SceneBrowserEmpty },
    { "tool_properties_idle", &EditorUi::Decor.ToolPropertiesIdle },
    { "console_empty", &EditorUi::Decor.ConsoleEmpty },
    { "status_tagline", &EditorUi::Decor.StatusTagline },
};

// The pristine palette, captured before the first theme load or reset so
// switching themes at runtime starts from the built-in defaults, not from
// whatever the previous theme left behind. The metrics need no capture: a
// default-constructed ChromeMetrics is the built-in set.
std::array<ImVec4, std::size(kThemeEntries)> kBuiltInPalette;
bool kBuiltInCaptured = false;

void CaptureBuiltIn()
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

void LoadColors(const JsonValue& colors, std::string& problems)
{
    for (const auto& [key, value] : colors.AsObject())
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
            problems += " unknown color '" + key + "';";
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
}

void LoadDecor(const JsonValue& decor, std::string& problems)
{
    for (const auto& [key, value] : decor.AsObject())
    {
        std::string* target = nullptr;
        for (const EditorThemeDecorEntry& entry : kThemeDecorEntries)
            if (key == entry.Key)
            {
                target = entry.Text;
                break;
            }
        if (target == nullptr)
        {
            problems += " unknown decor '" + key + "';";
            continue;
        }
        if (!value.IsString())
        {
            problems += " bad value for '" + key + "';";
            continue;
        }
        *target = value.AsString();
    }
}

// JSON string escaping for the handful of characters decor copy can hold.
std::string Quoted(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        default: out += c; break;
        }
    }
    out += '"';
    return out;
}

void LoadMetrics(const JsonValue& metrics, std::string& problems)
{
    for (const auto& [key, value] : metrics.AsObject())
    {
        float* target = nullptr;
        for (const EditorThemeMetricEntry& entry : kThemeMetricEntries)
            if (key == entry.Key)
            {
                target = entry.Value;
                break;
            }
        if (target == nullptr)
        {
            problems += " unknown metric '" + key + "';";
            continue;
        }
        if (!value.IsNumber())
        {
            problems += " bad value for '" + key + "';";
            continue;
        }
        *target = static_cast<float>(value.AsNumber());
    }
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
    CaptureBuiltIn();
    return kThemeEntries;
}

std::span<const EditorThemeMetricEntry> EditorThemeMetrics()
{
    CaptureBuiltIn();
    return kThemeMetricEntries;
}

std::span<const EditorThemeDecorEntry> EditorThemeDecor()
{
    return kThemeDecorEntries;
}

void ResetEditorTheme()
{
    CaptureBuiltIn();
    for (std::size_t i = 0; i < std::size(kThemeEntries); ++i)
        *kThemeEntries[i].Color = kBuiltInPalette[i];
    EditorUi::Metrics = EditorUi::ChromeMetrics{};
    EditorUi::Decor = EditorUi::DecorStrings{};
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
    file << "  },\n  \"metrics\": {\n";
    for (std::size_t i = 0; i < std::size(kThemeMetricEntries); ++i)
    {
        char number[32];
        std::snprintf(number, sizeof(number), "%g", static_cast<double>(*kThemeMetricEntries[i].Value));
        file << "    \"" << kThemeMetricEntries[i].Key << "\": " << number
             << (i + 1 < std::size(kThemeMetricEntries) ? ",\n" : "\n");
    }
    file << "  },\n  \"decor\": {\n";
    for (std::size_t i = 0; i < std::size(kThemeDecorEntries); ++i)
        file << "    \"" << kThemeDecorEntries[i].Key << "\": " << Quoted(*kThemeDecorEntries[i].Text)
             << (i + 1 < std::size(kThemeDecorEntries) ? ",\n" : "\n");
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
    CaptureBuiltIn();
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
    const JsonValue* metrics = root->Find("metrics");
    const JsonValue* decor = root->Find("decor");
    const auto isObject = [](const JsonValue* v) { return v != nullptr && v->IsObject(); };
    if (!isObject(colors) && !isObject(metrics) && !isObject(decor))
    {
        if (error != nullptr)
            *error = "theme '" + path.string() + "' has no \"colors\", \"metrics\", or \"decor\" object";
        return false;
    }

    // A theme file describes the full look: keys it omits fall back to the
    // built-in default, not to whatever the previously loaded theme set.
    ResetEditorTheme();

    std::string problems;
    if (colors != nullptr)
    {
        if (colors->IsObject())
            LoadColors(*colors, problems);
        else
            problems += " \"colors\" is not an object;";
    }
    if (metrics != nullptr)
    {
        if (metrics->IsObject())
            LoadMetrics(*metrics, problems);
        else
            problems += " \"metrics\" is not an object;";
    }
    if (decor != nullptr)
    {
        if (decor->IsObject())
            LoadDecor(*decor, problems);
        else
            problems += " \"decor\" is not an object;";
    }

    if (!problems.empty() && error != nullptr)
        *error = "theme '" + path.string() + "':" + problems;
    return true;
}
