#pragma once

#include "ui/UiScreenSlot.h"

#include "overlay/ViewMarks.h"
#include "viewport/ViewportArrangement.h"

#include <render/ui/UiDrawFrame.h>
#include <ui/UiSurface.h>

#include <array>
#include <cstddef>
#include <vector>

class EditorRenderFeature;
class UiService;
class ViewportLayout;
struct EditorOverlayState;
struct MarqueeState;

//=============================================================================
// LevelViewSurface
// The Level's views on the content surface, each a host image at its tile.
// Laying them out gives each view its rect and target size, so an unshown view
// neither renders nor takes input.
//=============================================================================
class LevelViewSurface
{
public:
    static constexpr std::size_t kSlots = 4;

    // `render` may be null when the renderer never came up: the views are then
    // laid out and take input, and show nothing.
    LevelViewSurface(UiService& ui, ViewportLayout& layout, const EditorOverlayState& overlay,
                     const MarqueeState& marquee, EditorRenderFeature* render);
    ~LevelViewSurface();

    LevelViewSurface(const LevelViewSurface&) = delete;
    LevelViewSurface& operator=(const LevelViewSurface&) = delete;

    // The content surface of the window the workspace shows in.
    void MoveTo(UiSurfaceId content);
    // A hidden workspace keeps no screen open and shows no views.
    void SetShown(bool shown);

    // Per frame, before the UI updates and after the overlay is rebuilt: lays
    // the views `kind` shows over `content`, in window pixels, meeting at
    // `divider`, and marks them. An empty rect shows none. `scale` is the UI
    // scale tools lay out at.
    void Arrange(const Rect2d& content, ViewportArrangementKind kind, Vec2d divider, float scale);

private:
    void ShowMarks(float scale);

    UiService& Ui;
    ViewportLayout& Layout;
    const EditorOverlayState& Overlay;
    const MarqueeState& Marquee;
    EditorRenderFeature* Render = nullptr;
    std::vector<ViewMark> Marks;
    UiScreenSlot Screen;
    bool Shown = false;
    std::array<UiHostImageId, kSlots> Images{};
};
