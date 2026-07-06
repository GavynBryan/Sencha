#pragma once

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

class ConsoleRegistry;

// The View > Preferences > Theme UI: lists the theme JSONs in the bundled themes
// directory (each with a color swatch preview), applies a choice live, and hosts
// the palette window where individual colors are overridden in place and saved
// back out as a theme file. The chosen theme name is written to the
// editor.ui.theme cvar, the same value the startup path reads.
class ThemePreferences
{
public:
    explicit ThemePreferences(std::filesystem::path themeDir);

    // The contents of the "Theme" submenu; call between BeginMenu/EndMenu.
    void DrawMenu(ConsoleRegistry& console);
    // The palette override window, when open. Call once per frame, outside the
    // menu bar.
    void DrawWindow(ConsoleRegistry& console);

private:
    struct ThemeChoice
    {
        std::string Name; // file stem; doubles as the editor.ui.theme value
        std::filesystem::path Path;
        std::vector<ImVec4> Swatches; // preview colors parsed from the file
    };

    void Rescan();
    // Resets the palette, loads the named theme ("" = built-in), reapplies the
    // ImGui style, and records the choice in the cvar.
    void ApplyChoice(ConsoleRegistry& console, const std::string& name);
    void SetThemeCVar(ConsoleRegistry& console, const std::string& name);

    std::filesystem::path ThemeDir;
    std::vector<ThemeChoice> Themes;
    std::string ActiveName; // "" = built-in defaults
    bool Scanned = false;
    bool WindowOpen = false;
    char SaveName[64] = "custom";
    std::string Status; // last load/save problem, surfaced in the window
};
