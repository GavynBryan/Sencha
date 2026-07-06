#pragma once

#include "IEditorPanel.h"

#include <debug/ConsoleView.h>

class ConsoleService;
class DebugLogSink;

// Editor console panel: the same body as the runtime ConsolePanel (shared via
// DrawConsoleView) styled with the editor palette and mono font, docked under
// the central viewport.
class EditorConsolePanel : public IEditorPanel
{
public:
    EditorConsolePanel(DebugLogSink& sink, ConsoleService& console);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::CenterBottom; }
    // Shares one tabbed node with the Materials browser in the center-bottom
    // strip.
    int GetDockTabGroup() const override { return 0; }

private:
    DebugLogSink& Sink;
    ConsoleService& Console;
    ConsoleViewState State;
    // Last ImGui frame this panel drew; a gap means it was just shown again, so
    // raise its tab (it may be docked behind another panel in its tab group).
    int LastDrawnFrame = -1;
};
