#pragma once

#include "overlay/ViewMarks.h"
#include "viewport/ViewportArrangement.h"

#include <render/ui/UiDrawFrame.h>
#include <ui/UiScreenHandle.h>
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
//
// The Level's views on the window's content surface: what the arrangement
// shows, each view's rendered image drawn as a host image at its tile, and
// what tools and drags put at points over them. Laying the views out is also
// what gives each its on-screen rect and its target size, so a view nobody
// shows neither renders nor takes input.
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
    void Open();
    void Close();
    void ShowMarks(float scale);

    UiService& Ui;
    ViewportLayout& Layout;
    const EditorOverlayState& Overlay;
    const MarqueeState& Marquee;
    EditorRenderFeature* Render = nullptr;
    std::vector<ViewMark> Marks;
    UiSurfaceId Surface;
    UiScreenHandle Screen;
    bool Shown = false;
    std::array<UiHostImageId, kSlots> Images{};
};
