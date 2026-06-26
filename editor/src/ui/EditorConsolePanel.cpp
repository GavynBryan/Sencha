#include "EditorConsolePanel.h"

#include "EditorUiStyle.h"
#include "ScopedPanel.h"

#include <debug/ConsoleView.h>

namespace
{
    ConsoleViewStyle EditorStyle()
    {
        ConsoleViewStyle style;
        style.LevelColors = {
            EditorUi::TextDim,     // Debug
            EditorUi::TextPrimary, // Info
            EditorUi::Warning,     // Warning
            EditorUi::Danger,      // Error
            EditorUi::Critical,    // Critical
        };
        style.TextPrimary = EditorUi::TextPrimary;
        style.Warning = EditorUi::Warning;
        style.Error = EditorUi::Danger;
        style.Mono = EditorUi::MonoFont();
        style.ShowCopy = true;
        return style;
    }
}

EditorConsolePanel::EditorConsolePanel(DebugLogSink& sink, ConsoleService& console)
    : Sink(sink)
    , Console(console)
{
}

std::string_view EditorConsolePanel::GetTitle() const
{
    return "Console";
}

void EditorConsolePanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    DrawConsoleView(Sink, Console, State, EditorStyle());
}
