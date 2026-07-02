#pragma once

#include "IEditorPanel.h"

#include <debug/ConsoleView.h>

class ConsoleService;
class DebugLogSink;

// Editor console panel: the same body as the runtime ConsolePanel (shared via
// DrawConsoleView) styled with the editor palette and mono font, docked at the
// bottom of the editor layout.
class EditorConsolePanel : public IEditorPanel
{
public:
    EditorConsolePanel(DebugLogSink& sink, ConsoleService& console);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }

private:
    DebugLogSink& Sink;
    ConsoleService& Console;
    ConsoleViewState State;
};
