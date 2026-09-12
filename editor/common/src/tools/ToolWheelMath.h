#pragma once

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <numbers>

// Where a radial tool menu lands on screen and which sector a point is in.
// Pure pixel math with no ImGui runtime, so the session that hit-tests the
// wheel and the chrome that paints it go through one rule rather than two
// versions of it that drift apart.
//
// Sizes are design pixels resolved exactly once, in Place, from the frame's
// scale: a Layout holds nothing but physical pixels, and nothing downstream
// scales anything again.
namespace ToolWheel
{
inline constexpr float kRadius = 72.0f;        // hub centre to a slot's centre
inline constexpr float kHub = 26.0f;           // the dead zone: releasing here selects nothing
inline constexpr float kButton = 26.0f;        // one slot, square
inline constexpr float kCaptionGap = 8.0f;     // between the lowest slot and the caption
inline constexpr float kCaptionHeight = 18.0f; // the caption's line

// What the wheel is placed against: the window's work area and the UI scale,
// captured together at the moment the wheel opens.
struct Frame
{
    float Scale = 1.0f;
    ImVec2 Min = {};
    ImVec2 Max = {};
};

struct Layout
{
    ImVec2 Center = {};
    float Radius = 0.0f;
    float Hub = 0.0f;
    float Button = 0.0f;
    float CaptionGap = 0.0f;
    float CaptionHeight = 0.0f;
    int Count = 0;

    // Where the caption's line starts, under the ring.
    [[nodiscard]] float CaptionY() const { return Center.y + Radius + Button * 0.5f + CaptionGap; }
    // The half-extents the wheel needs clear of the frame's edges.
    [[nodiscard]] float HalfWidth() const { return Radius + Button * 0.5f; }
    [[nodiscard]] float HalfHeightAbove() const { return Radius + Button * 0.5f; }
    [[nodiscard]] float HalfHeightBelow() const { return Radius + Button * 0.5f + CaptionGap + CaptionHeight; }
};

struct Span
{
    float Begin = 0.0f; // radians, clockwise from straight up
    float End = 0.0f;
};

// The angular span of sector `index`: sector 0 is centred straight up and the
// sectors proceed clockwise, so the tools read in registry order like a clock.
[[nodiscard]] inline Span SectorSpan(int index, int count)
{
    if (count <= 0)
        return {};
    const float width = 2.0f * std::numbers::pi_v<float> / static_cast<float>(count);
    const float begin = static_cast<float>(index) * width - width * 0.5f;
    return { begin, begin + width };
}

// A wheel of `count` slots opened at `pointer`, shifted -- never clipped -- so
// the whole of it (ring, slots, caption) lies inside the frame. A frame too
// small on an axis centres the wheel on that axis instead.
[[nodiscard]] inline Layout Place(const Frame& frame, ImVec2 pointer, int count)
{
    Layout layout;
    layout.Radius = kRadius * frame.Scale;
    layout.Hub = kHub * frame.Scale;
    layout.Button = kButton * frame.Scale;
    layout.CaptionGap = kCaptionGap * frame.Scale;
    layout.CaptionHeight = kCaptionHeight * frame.Scale;
    layout.Count = std::max(count, 0);

    const auto fit = [](float wanted, float lo, float hi, float before, float after)
    {
        if (hi - lo < before + after)
            return (lo + hi) * 0.5f;
        return std::clamp(wanted, lo + before, hi - after);
    };
    layout.Center.x = fit(pointer.x, frame.Min.x, frame.Max.x, layout.HalfWidth(), layout.HalfWidth());
    layout.Center.y = fit(pointer.y, frame.Min.y, frame.Max.y, layout.HalfHeightAbove(), layout.HalfHeightBelow());
    return layout;
}

// The centre of slot `index`, on the ring.
[[nodiscard]] inline ImVec2 SlotCenter(const Layout& layout, int index)
{
    const Span span = SectorSpan(index, layout.Count);
    const float angle = (span.Begin + span.End) * 0.5f;
    return { layout.Center.x + std::sin(angle) * layout.Radius, layout.Center.y - std::cos(angle) * layout.Radius };
}

// The sector `point` is in, or -1 inside the hub (or with nothing to choose).
// Only the direction from the centre matters beyond the hub: the whole wedge,
// out to the edge of the screen, is the target.
[[nodiscard]] inline int SectorAt(const Layout& layout, ImVec2 point)
{
    if (layout.Count <= 0)
        return -1;
    const float dx = point.x - layout.Center.x;
    const float dy = point.y - layout.Center.y;
    if (dx * dx + dy * dy < layout.Hub * layout.Hub)
        return -1;
    // Clockwise from straight up, in [0, 2pi).
    float angle = std::atan2(dx, -dy);
    if (angle < 0.0f)
        angle += 2.0f * std::numbers::pi_v<float>;
    const float width = 2.0f * std::numbers::pi_v<float> / static_cast<float>(layout.Count);
    const int index = static_cast<int>(std::floor((angle + width * 0.5f) / width));
    return index % layout.Count;
}
} // namespace ToolWheel
