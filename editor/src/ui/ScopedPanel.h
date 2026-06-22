#pragma once

#include "EditorUiSkin.h"

#include <imgui.h>

#include <string_view>

//=============================================================================
// ScopedPanel — RAII for an IEditorPanel body. Begins the window (applying the
// shared PanelBackdrop when open); End() always runs in the destructor, which
// ImGui requires even when Begin returns false and makes every early-return in
// the body safe. Usage:
//
//   void XxxPanel::OnDraw() {
//       ScopedPanel panel(GetTitle(), &Visible);
//       if (!panel.IsOpen()) return;   // End() still fires via the dtor
//       ... body, may early-return freely ...
//   }
//=============================================================================

class ScopedPanel
{
public:
    ScopedPanel(std::string_view title, bool* open, ImGuiWindowFlags flags = 0)
        : Open(ImGui::Begin(title.data(), open, flags))
    {
        if (Open)
            EditorUiSkin::PanelBackdrop();
    }

    ~ScopedPanel() { ImGui::End(); }

    ScopedPanel(const ScopedPanel&) = delete;
    ScopedPanel& operator=(const ScopedPanel&) = delete;

    [[nodiscard]] bool IsOpen() const { return Open; }

private:
    bool Open;
};
