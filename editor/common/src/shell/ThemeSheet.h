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
//
// The editor's theme: the palette the editor.ui.theme cvar names -- a theme in
// the bundled themes directory, or a path to a theme JSON -- applied at the
// frame boundary, and the authored UI's theme stylesheet generated from it.
// Each bundled theme is a theme.use.* action; choosing one sets the cvar.
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
