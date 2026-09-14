#pragma once

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
// The panel is mounted in a chamfered frame of the weight it names, with a
// header rail under its tab, drawn around whatever the body does: the well
// goes under the content and the ring, rail, and ornaments over it, so the
// body never keeps clear of its chrome.
//=============================================================================

class ScopedPanel
{
public:
    ScopedPanel(std::string_view title, bool* open, PanelStyle style = PanelStyle::Standard,
                ImGuiWindowFlags flags = 0);
    ~ScopedPanel();

    ScopedPanel(const ScopedPanel&) = delete;
    ScopedPanel& operator=(const ScopedPanel&) = delete;

    [[nodiscard]] bool IsOpen() const { return Open; }

private:
    bool Open = false;
    PanelStyle Style = PanelStyle::Standard;
    ImVec2 Min{}; // the frame's rect, fixed at Begin so the edges land where the base did
    ImVec2 Max{};
};
