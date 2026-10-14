#pragma once

#include <platform/WindowTypes.h>

#include <array>
#include <cstddef>
#include <cstdint>

// The frame of a window that draws its own decorations: where the application
// says its caption is, classified against the window's own state when the
// platform asks. Fixed storage: the probe runs inside event dispatch.
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
    // A button is down or the pointer is relative: the application has it.
    bool PointerHeld = false;
};

// Edges and corners (of a resizable, unmaximized, bordered window) win over an
// enabled caption; everything else is Client, and so is everything while the
// pointer is held (docs/ui/architecture.md §6).
[[nodiscard]] WindowFrameHit ClassifyWindowFrameHit(const WindowFrameRegions& regions, const WindowFrameProbe& probe);
