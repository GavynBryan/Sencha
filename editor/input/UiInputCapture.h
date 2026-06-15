#pragma once

// Which input devices the editor's UI layer currently owns. The UI layer (the
// authority on this — it owns the ImGui context) reports it; the input router
// consumes events for a device the UI owns before any viewport tool sees them.
//
// Deliberately free of any UI-backend or router dependency: it is the small,
// stable vocabulary both layers share. If the UI backend ever changes from Dear
// ImGui, only the layer that produces this struct changes — not the input system.
struct UiInputCapture
{
    bool Mouse = false;
    bool Keyboard = false;
};
