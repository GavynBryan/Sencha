#pragma once

#include "ui/IEditorPanel.h"
#include "ui/chrome/PanelStyle.h"

#include "viewport/ViewportLayout.h"

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

struct MarqueeState;
struct EditorOverlayState;
class ViewportTargetCache;

// One dock panel drawing one viewport. Instantiated per view (the central
// perspective panel, the center-bottom ortho panel); title, dock placement,
// chrome composition, and the viewport it presents are construction data, not
// subclasses. The ortho view's orientation is switched by a header combo; the
// perspective panel shows a label there.
//
// The panel names a composition and asks the chrome kit for what it implies --
// how tall its header is, what boundary its content carries. It never tests
// which composition it was given.
class ViewportPanel : public IEditorPanel
{
public:
    ViewportPanel(ViewportLayout& layout, const MarqueeState& marquee, const EditorOverlayState& overlay,
                  ViewportTargetCache& targets, std::string title, DockSlot slot, float dockWeight,
                  PanelStyle style, PanelPersistence persistence, ViewportId viewport);

    std::string_view GetTitle() const override { return Title; }
    PanelPersistence GetPersistence() const override { return Persistence; }
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return Slot; }
    float GetDockWeight() const override { return Weight; }

    [[nodiscard]] ViewportId GetViewportId() const { return Viewport; }

    // True when the cursor is over this panel's 3D render region with no UI panel
    // on top of it (z-order aware). The viewport region is a passthrough hole in
    // the UI: input there belongs to the scene/tools, not the UI. Reflects the
    // last drawn frame.
    [[nodiscard]] bool IsViewportRegionHovered() const { return RegionHovered; }

    // Something dropped a scene source onto the render region: the viewport it
    // landed in, the drop position in screen pixels, and the asset:// path the
    // payload carried. The composition root installs the handler that picks a
    // point and places; the panel only reports the drop.
    using SceneDropHandler =
        std::function<void(ViewportId, ImVec2, std::string_view)>;
    void SetSceneDropHandler(SceneDropHandler handler)
    {
        SceneDrop = std::move(handler);
    }

    // Rows of chrome the panel reserves at its top, drawn by whoever owns
    // what is on them: the panel reserves each height, hands over the rect,
    // and never learns what a row holds. Rows installed here are the panel's
    // header, top to bottom, in place of the title row; a panel with none
    // keeps the title row (the ortho view's orientation combo lives there).
    // The scene rect starts under them, so everything that reads it follows.
    struct ChromeRow
    {
        std::function<float()> Height;
        std::function<void(ImDrawList* dl, ImVec2 mn, ImVec2 mx)> Draw;
    };
    void SetHeaderRows(std::vector<ChromeRow> rows) { Rows = std::move(rows); }

    // Zeroes the viewport's on-screen rect so ResolveAt cannot route input to a
    // view that is not being drawn. The composition root calls this each frame
    // the panel is hidden (OnDraw only runs for visible panels, so the panel
    // cannot clear its own stale rect).
    void ClearViewportRegion();

private:
    void DrawViewport(EditorViewport& viewport, ImVec2 size);
    // World-anchored overlay (selection dimension labels + active drag readout),
    // drawn into the viewport's ImGui draw list via screen projection.
    void DrawOverlay(const EditorViewport& viewport, ImDrawList* drawList);
    void DrawOrientationSelector(EditorViewport& viewport);
    void DrawChromeRow(const ChromeRow& row);

    ViewportLayout& Layout;
    const MarqueeState& Marquee;
    const EditorOverlayState& Overlay;
    ViewportTargetCache& Targets;
    std::string Title;
    DockSlot Slot;
    float Weight;
    PanelStyle Style;
    PanelPersistence Persistence;
    ViewportId Viewport;
    bool RegionHovered = false;
    SceneDropHandler SceneDrop;
    std::vector<ChromeRow> Rows;
};
