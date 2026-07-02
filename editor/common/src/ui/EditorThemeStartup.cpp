#include "ui/EditorThemeStartup.h"

#include "ui/EditorThemeFile.h"

#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>

#ifndef SENCHA_EDITOR_THEME_DIR
#define SENCHA_EDITOR_THEME_DIR "."
#endif

void ApplyEditorThemeFromConsole(ConsoleService& console)
{
    console.Registry().RegisterCVar({
        .Name = "editor.ui.theme",
        .Owner = "editor",
        .Type = CVarType::String,
        .DefaultValue = std::string{},
        .CurrentValue = std::string{},
        .Flags = CVarFlags::Archive,
        .Help = "Editor chrome theme: a name under the bundled themes/ dir or a path to a theme JSON. Empty = built-in. Applied at startup.",
        .Source = { "editor" },
    });

    const CVarMetadata* themeVar = console.Registry().FindCVar("editor.ui.theme");
    if (themeVar == nullptr)
        return;
    const std::string* name = std::get_if<std::string>(&themeVar->CurrentValue);
    if (name == nullptr || name->empty())
        return;

    std::filesystem::path themePath(*name);
    std::error_code ec;
    if (!std::filesystem::exists(themePath, ec))
        themePath = std::filesystem::path(SENCHA_EDITOR_THEME_DIR) / (*name + ".json");
    std::string themeError;
    if (!LoadEditorTheme(themePath, &themeError) || !themeError.empty())
        std::fprintf(stderr, "[editor] %s\n", themeError.c_str());
}
