#pragma once

#include <string_view>

// Where a panel wants to live in the default dock layout. The dockspace host
// (EditorUiFeature) groups panels by slot and arranges them, so placement stays
// declarative — a new panel just picks a slot, no central layout table to edit.
// Floating panels are left undocked.
enum class DockSlot
{
    Center,
    Left,
    Right,
    Bottom,
    Floating
};

struct IEditorPanel
{
    virtual std::string_view GetTitle() const = 0;
    virtual void OnDraw() = 0;

    // Preferred default dock slot; the host may override once the user rearranges.
    [[nodiscard]] virtual DockSlot GetDockSlot() const { return DockSlot::Floating; }

    // Shared show/hide state (the View menu toggles this uniformly). Panels pass
    // &Visible to ImGui::Begin's p_open so the window close box stays in sync.
    [[nodiscard]] bool IsVisible() const { return Visible; }
    void SetVisible(bool visible) { Visible = visible; }
    void ToggleVisible() { Visible = !Visible; }

    virtual ~IEditorPanel() = default;

protected:
    bool Visible = true;
};
