#pragma once

#include <string_view>

// Where a panel wants to live in the default dock layout. The dockspace host
// (EditorUiFeature) groups panels by slot and arranges them, so placement stays
// declarative — a new panel just picks a slot, no central layout table to edit.
// Regions no panel requests are never created; floating panels are left undocked.
enum class DockSlot
{
    Center,       // central node (no tab bar)
    CenterBottom, // strip under the central node, same width
    Left,         // left column, full height of the main row
    Right,        // upper right, panels pack left-to-right
    RightBottom,  // lower right, panels stack top-to-bottom
    Bottom,       // full-width strip across the window bottom
    Floating
};

struct IEditorPanel
{
    virtual std::string_view GetTitle() const = 0;
    virtual void OnDraw() = 0;

    // Preferred default dock slot; the host may override once the user rearranges.
    [[nodiscard]] virtual DockSlot GetDockSlot() const { return DockSlot::Floating; }

    // Relative share of the slot when several panels pack into it (a weight-2
    // panel gets twice the space of a weight-1 neighbor).
    [[nodiscard]] virtual float GetDockWeight() const { return 1.0f; }

    // Shared show/hide state (the View menu toggles this uniformly). Panels pass
    // &Visible to ImGui::Begin's p_open so the window close box stays in sync.
    [[nodiscard]] bool IsVisible() const { return Visible; }
    void SetVisible(bool visible) { Visible = visible; }
    void ToggleVisible() { Visible = !Visible; }

    virtual ~IEditorPanel() = default;

protected:
    bool Visible = true;
};
