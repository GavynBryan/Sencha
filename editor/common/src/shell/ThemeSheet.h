#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class ActionCatalog;
class ActionContext;
class ConsoleRegistry;
class ProblemList;
class UiService;

//=============================================================================
// ThemeSheet
// The palette the editor.ui.theme cvar names, applied at the frame boundary, and
// the stylesheet generated from it; each bundled theme is a theme.use.* action.
//=============================================================================
class ThemeSheet
{
public:
    // Registers editor.ui.theme; `defaultTheme` empty is the built-in palette.
    ThemeSheet(ConsoleRegistry& console, ProblemList& problems, std::filesystem::path themeDir,
               std::string defaultTheme = {});

    void DefineActions(ActionCatalog& catalog, ActionContext& context);
    // At the frame boundary: the theme the cvar names, applied when it changed,
    // and the stylesheet published to `ui` (null until the UI is ready).
    void Update(UiService* ui);

private:
    [[nodiscard]] std::string ChosenTheme() const;
    void Apply(const std::string& name);

    ConsoleRegistry& Console;
    ProblemList& Problems;
    std::filesystem::path ThemeDir;
    // The bundled themes, by file stem.
    std::vector<std::string> Names;
    // The theme last applied; nothing before the first boundary.
    std::optional<std::string> Applied;
    bool Published = false;
};
