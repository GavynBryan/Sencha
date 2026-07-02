#include "ThemePreferences.h"

#include "EditorThemeFile.h"
#include "EditorUiStyle.h"

#include <core/console/ConsoleRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

namespace
{
// The palette keys shown as a theme's preview strip, background to accent.
constexpr const char* kSwatchKeys[] = { "window_bg", "header_bg", "accent", "secondary", "text_primary" };

// Swatch previews and pickers work by eye: the palette stores LINEAR values and
// the sRGB swapchain encodes on write, so the rendered swatch is the authored
// color. The numeric fields would show the linear encoding, so they stay hidden.
constexpr ImGuiColorEditFlags kSwatchFlags =
    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop;
constexpr ImGuiColorEditFlags kEditFlags =
    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf;

std::vector<ImVec4> ReadPreviewSwatches(const std::filesystem::path& path)
{
    std::vector<ImVec4> swatches;
    std::ifstream file(path);
    if (!file.is_open())
        return swatches;
    std::stringstream buffer;
    buffer << file.rdbuf();

    const std::optional<JsonValue> root = JsonParse(buffer.str(), nullptr);
    if (!root.has_value())
        return swatches;
    const JsonValue* colors = root->Find("colors");
    if (colors == nullptr || !colors->IsObject())
        return swatches;

    for (const char* key : kSwatchKeys)
    {
        const JsonValue* value = colors->Find(key);
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
        if (value != nullptr && value->IsString() && ParseThemeColor(value->AsString(), r, g, b, a))
            swatches.push_back(ImVec4(r, g, b, a));
    }
    return swatches;
}
}

ThemePreferences::ThemePreferences(std::filesystem::path themeDir)
    : ThemeDir(std::move(themeDir))
{
}

void ThemePreferences::Rescan()
{
    Themes.clear();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(ThemeDir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        Themes.push_back(ThemeChoice{
            .Name = entry.path().stem().string(),
            .Path = entry.path(),
            .Swatches = ReadPreviewSwatches(entry.path()),
        });
    }
    std::sort(Themes.begin(), Themes.end(),
              [](const ThemeChoice& a, const ThemeChoice& b) { return a.Name < b.Name; });
    Scanned = true;
}

void ThemePreferences::SetThemeCVar(ConsoleRegistry& console, const std::string& name)
{
    (void)console.SetCVar("editor.ui.theme", name, ConsoleValueSource{ .Description = "preferences menu" },
                          ConsolePhase::GameplayStarted);
}

void ThemePreferences::ApplyChoice(ConsoleRegistry& console, const std::string& name)
{
    ResetEditorThemePalette();
    Status.clear();
    if (!name.empty())
    {
        const auto it = std::find_if(Themes.begin(), Themes.end(),
                                     [&name](const ThemeChoice& t) { return t.Name == name; });
        if (it == Themes.end())
            return;
        std::string error;
        if (!LoadEditorTheme(it->Path, &error) || !error.empty())
            Status = error;
    }
    EditorUi::Apply(ImGui::GetStyle());
    ActiveName = name;
    SetThemeCVar(console, name);
}

void ThemePreferences::DrawMenu(ConsoleRegistry& console)
{
    if (!Scanned)
    {
        Rescan();
        // Pick up the startup theme so the check mark starts on the right entry.
        if (const CVarMetadata* var = console.FindCVar("editor.ui.theme"))
            if (const std::string* name = std::get_if<std::string>(&var->CurrentValue))
                ActiveName = *name;
    }

    if (ImGui::MenuItem("Built-in", nullptr, ActiveName.empty()))
        ApplyChoice(console, std::string{});

    for (const ThemeChoice& theme : Themes)
    {
        for (std::size_t i = 0; i < theme.Swatches.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::ColorButton(("##swatch_" + theme.Name).c_str(), theme.Swatches[i], kSwatchFlags,
                               ImVec2(ImGui::GetFontSize() * 0.75f, ImGui::GetFontSize() * 0.75f));
            ImGui::PopID();
            ImGui::SameLine(0.0f, 2.0f);
        }
        if (ImGui::MenuItem(theme.Name.c_str(), nullptr, theme.Name == ActiveName))
            ApplyChoice(console, theme.Name);
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Customize Palette..."))
        WindowOpen = true;
    if (ImGui::MenuItem("Rescan Themes"))
        Rescan();
}

void ThemePreferences::DrawWindow(ConsoleRegistry& console)
{
    if (!WindowOpen)
        return;

    ImGui::SetNextWindowSize(ImVec2(360.0f, 540.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Theme Palette", &WindowOpen))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(ActiveName.empty() ? "Base theme: Built-in"
                                              : ("Base theme: " + ActiveName).c_str());
    ImGui::Separator();

    bool edited = false;
    for (const EditorThemePaletteEntry& entry : EditorThemePalette())
        edited |= ImGui::ColorEdit4(entry.Key, &entry.Color->x, kEditFlags);
    if (edited)
        EditorUi::Apply(ImGui::GetStyle());

    ImGui::Separator();
    if (ImGui::Button("Revert to Base Theme"))
        ApplyChoice(console, ActiveName);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    ImGui::InputText("##theme_name", SaveName, sizeof(SaveName));
    ImGui::SameLine();
    if (ImGui::Button("Save as Theme") && SaveName[0] != '\0')
    {
        const std::string name(SaveName);
        std::string error;
        if (SaveEditorTheme(ThemeDir / (name + ".json"), &error))
        {
            Rescan();
            ActiveName = name;
            SetThemeCVar(console, name);
            Status = "saved " + name + ".json";
        }
        else
        {
            Status = error;
        }
    }

    if (!Status.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
        ImGui::TextWrapped("%s", Status.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}
