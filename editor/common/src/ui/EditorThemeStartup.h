#pragma once

class ConsoleService;

// Registers the editor.ui.theme cvar and applies the theme it names (a name
// under the bundled themes/ dir or a path to a theme JSON; empty keeps the
// built-in palette). Call before EditorUiFeature applies the ImGui style, in
// every editor application, so the whole family themes through one cvar.
void ApplyEditorThemeFromConsole(ConsoleService& console);
