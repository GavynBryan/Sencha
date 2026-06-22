#pragma once

#include <debug/ConsoleView.h>
#include <debug/IDebugPanel.h>

class ConsoleService;
class DebugLogSink;

//=============================================================================
// ConsolePanel
//
// Debug-overlay console: the captured log feed (per-level colors, level and
// category filters) plus a command input with history recall and autosuggest.
// The body is shared with the editor via DrawConsoleView; this adapter supplies
// the runtime style and the IDebugPanel seam.
//
// Registered with ImGuiDebugOverlay::AddPanel() during app setup.
//=============================================================================
class ConsolePanel : public IDebugPanel
{
public:
	ConsolePanel(DebugLogSink& sink, ConsoleService& console);

	void Draw() override;

private:
	DebugLogSink& Sink;
	ConsoleService& Console;
	ConsoleViewState State;
};
