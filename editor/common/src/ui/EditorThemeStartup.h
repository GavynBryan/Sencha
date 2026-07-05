#pragma once

#include <string_view>

class ConsoleService;

// Registers the editor.ui.theme cvar and applies the theme it names (a name
// under the bundled themes/ dir or a path to a theme JSON; empty keeps the
// built-in palette). Call before EditorUiFeature applies the ImGui style, in
// every editor application, so the whole family themes through one cvar.
// `defaultTheme` is the application's default when the cvar is not set to
// something else; empty defaults to the built-in palette.
void ApplyEditorThemeFromConsole(ConsoleService& console, std::string_view defaultTheme = {});
