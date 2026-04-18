#pragma once

#include <debug/IDebugPanel.h>
#include <core/logging/LogLevel.h>
#include <array>
#include <cstddef>

class DebugLogSink;

//=============================================================================
// ConsolePanel
//
// Debug panel that displays captured log entries from a DebugLogSink.
// Rendered as an ImGui window with:
//   - Per-level colour coding
//   - Level filter checkboxes (Debug / Info / Warning / Error / Critical)
//   - Source category filter text input
//   - Auto-scroll to newest entry
//   - Clear button
//
// Registered with ImGuiDebugOverlay::AddPanel() during app setup.
//=============================================================================
class ConsolePanel : public IDebugPanel
{
public:
	explicit ConsolePanel(DebugLogSink& sink);

	void Draw() override;

private:
	DebugLogSink& Sink;

	// Per-level visibility toggles (indexed by LogLevel cast to int).
	std::array<bool, 5> LevelFilter = { true, true, true, true, true };

	// Substring filter applied to the Category field.
	static constexpr std::size_t CategoryFilterBufSize = 128;
	char CategoryFilterBuf[CategoryFilterBufSize] = {};

	bool ScrollToBottom = true;
};
