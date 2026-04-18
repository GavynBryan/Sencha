#pragma once

//=============================================================================
// IDebugPanel
//
// Extension point for the debug overlay. Each panel is an independent,
// self-contained ImGui window or widget rendered while the debug UI is open.
//
// To add a new panel:
//   1. Derive from IDebugPanel.
//   2. Implement Draw() — called once per frame while debug mode is active.
//   3. Register it with ImGuiDebugOverlay::AddPanel().
//
// Panels must not be added or removed from within Draw().
//=============================================================================
class IDebugPanel
{
public:
	virtual ~IDebugPanel() = default;

	// Called once per frame while the debug overlay is open.
	// Use ImGui calls freely inside here.
	virtual void Draw() = 0;
};
