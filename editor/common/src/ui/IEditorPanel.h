#pragma once

#include <cstdint>
#include <string_view>

// Where a panel wants to live in the default dock layout. The dockspace host
// (EditorUiFeature) groups panels by slot and arranges them, so placement stays
// declarative — a new panel just picks a slot, no central layout table to edit.
// Regions no panel requests are never created; floating panels are left undocked.
enum class DockSlot
{
    Center,       // central node (no tab bar)
    CenterBottom, // strip under the central node, same width
    LeftEdge,     // narrow column at the far left of the main row; panels stack vertically
    Left,         // left column, full height of the main row
    Right,        // upper right, panels pack left-to-right
    RightBottom,  // lower right, panels stack top-to-bottom
    Bottom,       // full-width strip across the window bottom
    Floating
};

// Whether a panel's shown/hidden state is a workspace preference the shell
// remembers across launches, or belongs to the session it was opened in (a
// console whose startup state is configuration, a dialog-like panel a flow
// opens and closes).
enum class PanelVisibilityPolicy : std::uint8_t
{
    Remembered,
    SessionOnly,
};

// What the shell files a panel's settings under. The id is stable identity
// and never the title: a title is presentation and may change; a user's
// remembered layout must not.
struct PanelPersistence
{
    std::string_view Id;
    PanelVisibilityPolicy Visibility = PanelVisibilityPolicy::Remembered;
};

struct IEditorPanel
{
    virtual std::string_view GetTitle() const = 0;
    virtual void OnDraw() = 0;

    // Every panel declares its settings identity and visibility policy.
    [[nodiscard]] virtual PanelPersistence GetPersistence() const = 0;

    // Preferred default dock slot; the host may override once the user rearranges.
    [[nodiscard]] virtual DockSlot GetDockSlot() const { return DockSlot::Floating; }

    // Relative share of the slot when several panels pack into it (a weight-2
    // panel gets twice the space of a weight-1 neighbor).
    [[nodiscard]] virtual float GetDockWeight() const { return 1.0f; }

    // Panels in the same slot sharing a non-negative group dock into ONE node
    // (ImGui tabs them) instead of splitting it; -1 keeps a panel in its own
    // weighted slice. Groups are meaningful only within a slot.
    [[nodiscard]] virtual int GetDockTabGroup() const { return -1; }

    // Shared show/hide state (the View menu toggles this uniformly). Panels pass
    // &Visible to ImGui::Begin's p_open so the window close box stays in sync.
    [[nodiscard]] bool IsVisible() const { return Visible; }
    void SetVisible(bool visible) { Visible = visible; }
    void ToggleVisible() { Visible = !Visible; }

    virtual ~IEditorPanel() = default;

protected:
    bool Visible = true;
};
