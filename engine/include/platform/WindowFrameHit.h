#pragma once

#include <platform/WindowTypes.h>

#include <array>
#include <cstddef>
#include <cstdint>

// The frame of a window that draws its own decorations. The application
// publishes where its caption is and whether dragging from it is permitted
// right now; the window classifies a pointer position against that snapshot
// and its own state (size, resizable, maximized) when the platform asks what
// a press there means. Plain data with fixed storage: the probe runs inside
// the platform's event dispatch and must not allocate.
struct WindowRect
{
    int32_t X = 0;
    int32_t Y = 0;
    int32_t Width = 0;
    int32_t Height = 0;
};

enum class WindowFrameHit : uint8_t
{
    Client,
    Caption,
    ResizeLeft,
    ResizeRight,
    ResizeTop,
    ResizeBottom,
    ResizeTopLeft,
    ResizeTopRight,
    ResizeBottomLeft,
    ResizeBottomRight,
};

struct WindowFrameRegions
{
    static constexpr std::size_t kMaxCaptionRects = 2;
    std::array<WindowRect, kMaxCaptionRects> Caption{}; // where the caption is, window points
    std::size_t CaptionCount = 0;
    bool CaptionEnabled = false; // whether dragging from the caption is permitted right now
    int32_t ResizeBorder = 0;    // edge thickness in window points; 0 disables edge resizing
};

struct WindowFrameProbe
{
    int32_t X = 0;
    int32_t Y = 0;
    WindowExtent Extent;
    bool Resizable = false;
    bool Maximized = false;
};

// Resize edges and corners win over the caption (the caption touches the top
// edge, so its outer pixels resize rather than drag); edges apply only to a
// resizable, unmaximized window with a border; the caption applies only while
// enabled. Everything else is Client.
[[nodiscard]] WindowFrameHit ClassifyWindowFrameHit(const WindowFrameRegions& regions, const WindowFrameProbe& probe);
