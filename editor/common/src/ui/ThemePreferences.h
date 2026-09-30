#pragma once

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class ConsoleRegistry;

// Theme choice: the theme JSONs in the bundled themes directory, applying one
// live, and the palette window where individual colors are overridden in place
// and saved back out as a theme file. The chosen theme name is written to the
// editor.ui.theme cvar, the same value the startup path reads.
class ThemePreferences
{
public:
    explicit ThemePreferences(std::filesystem::path themeDir);

    // The themes on offer, by name; empty is the built-in one.
    [[nodiscard]] std::vector<std::string> ThemeNames(ConsoleRegistry& console);
    [[nodiscard]] const std::string& ActiveTheme() const { return ActiveName; }
    void Choose(ConsoleRegistry& console, const std::string& name) { RequestChoice(console, name); }
    void OpenPaletteWindow() { WindowOpen = true; }
    // The palette override window, when open. Call once per frame, outside the
    // menu bar.
    void DrawWindow(ConsoleRegistry& console);

    // Makes the editor.ui.theme cvar and the loaded theme agree. The startup
    // path applies a theme during Setup, but argv's +set lands after every
    // feature's Setup, so the cvar can name a theme that was never loaded.
    // Called once at the first frame boundary, where a theme change is
    // already safe to make.
    void SyncWithCVar(ConsoleRegistry& console);

    // Applies a theme the menu asked for, if any; true when one was applied.
    // Choosing a theme only records the request, because the menu is drawn
    // mid-frame: loading there would leave the rest of that frame drawing new
    // colors and metrics against resources resolved from the previous theme.
    // The host calls this at the frame boundary, before it resolves anything
    // derived from theme state, so one frame sees one coherent theme.
    bool CommitPending();

private:
    struct ThemeChoice
    {
        std::string Name; // file stem; doubles as the editor.ui.theme value
        std::filesystem::path Path;
        std::vector<ImVec4> Swatches; // preview colors parsed from the file
    };

    void Rescan();
    // Records a theme choice for the next frame boundary and writes the cvar.
    void RequestChoice(ConsoleRegistry& console, const std::string& name);
    void SetThemeCVar(ConsoleRegistry& console, const std::string& name);

    std::filesystem::path ThemeDir;
    std::vector<ThemeChoice> Themes;
    std::string ActiveName; // "" = built-in defaults
    // Set by a menu choice, consumed by CommitPending at the frame boundary.
    std::optional<std::string> Pending;
    bool Scanned = false;
    bool WindowOpen = false;
    char SaveName[64] = "custom";
    std::string Status; // last load/save problem, surfaced in the window
};
