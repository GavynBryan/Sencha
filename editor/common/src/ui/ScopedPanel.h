#pragma once

#include "EditorUiSkin.h"
#include "chrome/PanelStyle.h"

#include <imgui.h>

#include <string_view>

//=============================================================================
// ScopedPanel — RAII for an IEditorPanel body, and the one place a panel's
// chrome comes from. Begins the window; End() always runs in the destructor,
// which ImGui requires even when Begin returns false and makes every
// early-return in the body safe. Usage:
//
//   void XxxPanel::OnDraw() {
//       ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Standard);
//       if (!panel.IsOpen()) return;   // End() still fires via the dtor
//       ... body, may early-return freely ...
//   }
//
// With a PanelStyle the panel is mounted in a chamfered frame with a header
// rail, drawn around whatever the body does: the well goes under the content
// and the ring and rail over it, so the body never keeps clear of its chrome.
// Without one (the older form) the panel gets the previous gradient backdrop;
// that form goes away once every panel has moved over.
//=============================================================================

class ScopedPanel
{
public:
    ScopedPanel(std::string_view title, bool* open, ImGuiWindowFlags flags = 0);
    ScopedPanel(std::string_view title, bool* open, PanelStyle style, ImGuiWindowFlags flags = 0);
    ~ScopedPanel();

    ScopedPanel(const ScopedPanel&) = delete;
    ScopedPanel& operator=(const ScopedPanel&) = delete;

    [[nodiscard]] bool IsOpen() const { return Open; }

private:
    bool Open = false;
    bool Chrome = false;
    PanelStyle Style = PanelStyle::Standard;
    ImVec2 Min{}; // the frame's rect, fixed at Begin so the edges land where the base did
    ImVec2 Max{};
};
