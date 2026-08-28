#pragma once

#include "ui/IEditorPanel.h"

#include "viewport/ViewportLayout.h"

#include <imgui.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

struct MarqueeState;
struct EditorOverlayState;
class ViewportTargetCache;

// One dock panel drawing one viewport. Instantiated per view (the central
// perspective panel, the center-bottom ortho panel); title, dock placement,
// and the viewport it presents are construction data, not subclasses. The
// ortho view's orientation is switched by a header combo; the perspective
// panel shows a label there.
class ViewportPanel : public IEditorPanel
{
public:
    ViewportPanel(ViewportLayout& layout, const MarqueeState& marquee, const EditorOverlayState& overlay,
                  ViewportTargetCache& targets, std::string title, DockSlot slot, float dockWeight,
                  ViewportId viewport);

    std::string_view GetTitle() const override { return Title; }
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
    // Fills the panel area NOT covered by the 3D region rect (the header strip +
    // border gaps) with the dark panel color, so the bright engine clear color
    // stops bleeding through the transparent (NoBackground) panel.
    void FillGapsBehindViewports();

    ViewportLayout& Layout;
    const MarqueeState& Marquee;
    const EditorOverlayState& Overlay;
    ViewportTargetCache& Targets;
    std::string Title;
    DockSlot Slot;
    float Weight;
    ViewportId Viewport;
    bool RegionHovered = false;
    // 3D render-region rects collected this frame (the passthrough holes to keep
    // transparent); everything else in the panel gets the dark gap fill.
    std::vector<std::pair<ImVec2, ImVec2>> RegionRects;
    SceneDropHandler SceneDrop;
};
