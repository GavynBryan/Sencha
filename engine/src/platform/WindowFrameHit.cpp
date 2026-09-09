#include <platform/WindowFrameHit.h>

namespace
{
bool Contains(const WindowRect& rect, int32_t x, int32_t y)
{
    return rect.Width > 0 && rect.Height > 0 && x >= rect.X && y >= rect.Y && x < rect.X + rect.Width
        && y < rect.Y + rect.Height;
}
}

WindowFrameHit ClassifyWindowFrameHit(const WindowFrameRegions& regions, const WindowFrameProbe& probe)
{
    const int32_t width = static_cast<int32_t>(probe.Extent.Width);
    const int32_t height = static_cast<int32_t>(probe.Extent.Height);
    if (width <= 0 || height <= 0 || probe.X < 0 || probe.Y < 0 || probe.X >= width || probe.Y >= height)
        return WindowFrameHit::Client;

    if (probe.Resizable && !probe.Maximized && regions.ResizeBorder > 0)
    {
        const int32_t border = regions.ResizeBorder;
        const bool left = probe.X < border;
        const bool right = probe.X >= width - border;
        const bool top = probe.Y < border;
        const bool bottom = probe.Y >= height - border;
        if (top && left) return WindowFrameHit::ResizeTopLeft;
        if (top && right) return WindowFrameHit::ResizeTopRight;
        if (bottom && left) return WindowFrameHit::ResizeBottomLeft;
        if (bottom && right) return WindowFrameHit::ResizeBottomRight;
        if (top) return WindowFrameHit::ResizeTop;
        if (bottom) return WindowFrameHit::ResizeBottom;
        if (left) return WindowFrameHit::ResizeLeft;
        if (right) return WindowFrameHit::ResizeRight;
    }

    if (regions.CaptionEnabled)
    {
        const std::size_t count = regions.CaptionCount < WindowFrameRegions::kMaxCaptionRects
            ? regions.CaptionCount : WindowFrameRegions::kMaxCaptionRects;
        for (std::size_t i = 0; i < count; ++i)
            if (Contains(regions.Caption[i], probe.X, probe.Y))
                return WindowFrameHit::Caption;
    }
    return WindowFrameHit::Client;
}
