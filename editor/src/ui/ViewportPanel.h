#pragma once

#include "IEditorPanel.h"

#include "../viewport/ViewportLayout.h"

#include <imgui.h>

#include <utility>
#include <vector>

struct MarqueeState;

class ViewportPanel : public IEditorPanel
{
public:
    ViewportPanel(ViewportLayout& layout, const MarqueeState& marquee);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Center; }

    // True when the cursor is over a viewport's 3D render region with no UI panel
    // on top of it (z-order aware). The viewport region is a passthrough hole in
    // the UI: input there belongs to the scene/tools, not the UI. Reflects the
    // last drawn frame. (See docs/plans/sencha-level-editor/02 §5.3 input layering.)
    [[nodiscard]] bool IsViewportRegionHovered() const { return RegionHovered; }

private:
    void DrawLayoutToggle();
    void DrawNode(const LayoutNode& node, ImVec2 size);
    void DrawSingleView(ImVec2 size);
    void DrawViewport(EditorViewport& viewport, ImVec2 size, bool showOrientationSelector = true);
    void DrawOrientationSelector(EditorViewport& viewport);
    // Fills the panel area NOT covered by a 3D region rect (the splitter gaps + the
    // per-viewport header strips) with the dark panel color, so the bright engine
    // clear color stops bleeding through the transparent (NoBackground) panel.
    void FillGapsBehindViewports();

    ViewportLayout& Layout;
    const MarqueeState& Marquee;
    bool RegionHovered = false;
    // 3D render-region rects collected this frame (the passthrough holes to keep
    // transparent); everything else in the panel gets the dark gap fill.
    std::vector<std::pair<ImVec2, ImVec2>> RegionRects;
    // Set when switching into single mode so the orientation tab bar selects the
    // active viewport's current orientation once, instead of snapping to the first.
    bool SyncTabToOrientation = false;
};
