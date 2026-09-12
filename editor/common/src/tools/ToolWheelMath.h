#pragma once

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

// Where a radial tool menu lands on screen and which sector a point is in.
// Pure pixel math with no ImGui runtime, so the session that hit-tests the
// wheel and the chrome that paints it go through one rule rather than two
// versions of it that drift apart.
//
// One layout rule, two uses: slots laid evenly over an angular range at a
// radius. The primary ring is every tool over the full circle; a hot tool's
// variants are a compact fan outside the rim, each a standard wedge wide,
// centred on the tool's own direction, taking only the arc they need. The
// outer band exists in the layout whenever any tool has variants (the wheel
// reserves room for it at open and never moves afterwards); which fan is
// showing is the session's business.
//
// Sizes are design pixels resolved exactly once, in Place, from the frame's
// scale: a Layout holds nothing but physical pixels, and nothing downstream
// scales anything again.
namespace ToolWheel
{
inline constexpr float kRadius = 72.0f;        // hub centre to a slot's centre
inline constexpr float kHub = 26.0f;           // the dead zone: releasing here selects nothing
inline constexpr float kButton = 26.0f;        // one slot, square
inline constexpr float kSeam = 2.0f;           // the hairline between petals, and between petals and a rim
inline constexpr float kRim = 7.0f;            // the band of metal closing a ring
inline constexpr float kVariantWidth = std::numbers::pi_v<float> / 9.0f; // one variant wedge, radians (20 degrees)
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
    float Seam = 0.0f;
    float Rim = 0.0f;
    float CaptionGap = 0.0f;
    float CaptionHeight = 0.0f;
    // Hub centre to a variant slot's centre; 0 when no tool has variants.
    float OuterRadius = 0.0f;
    int Count = 0;
    // The most variants any tool has: 0 means the wheel is the primary ring alone.
    int MaxVariants = 0;

    // The outer edge of the primary ring's rim, where the variant ring begins.
    [[nodiscard]] float RimOuter() const { return Radius + Button * 0.5f + Seam + Rim; }
    // The outer edge of the variant ring's rim, or of the primary rim without one.
    [[nodiscard]] float OuterExtent() const
    {
        return MaxVariants > 0 ? OuterRadius + Button * 0.5f + Seam + Rim : RimOuter();
    }
    // Where the caption's line starts, under everything the wheel can show.
    [[nodiscard]] float CaptionY() const { return Center.y + OuterExtent() + CaptionGap; }
    // The half-extents the wheel needs clear of the frame's edges.
    [[nodiscard]] float HalfWidth() const { return OuterExtent(); }
    [[nodiscard]] float HalfHeightAbove() const { return OuterExtent(); }
    [[nodiscard]] float HalfHeightBelow() const { return OuterExtent() + CaptionGap + CaptionHeight; }
};

struct Span
{
    float Begin = 0.0f; // radians, clockwise from straight up
    float End = 0.0f;
};

// A range of angles, radians clockwise from straight up.
struct Range
{
    float Begin = 0.0f;
    float Width = 0.0f;
};

// Slot `index` of `count` laid evenly over `range`, in order clockwise.
[[nodiscard]] inline Span SlotSpan(Range range, int index, int count)
{
    if (count <= 0)
        return {};
    const float width = range.Width / static_cast<float>(count);
    const float begin = range.Begin + width * static_cast<float>(index);
    return { begin, begin + width };
}

// The slot of `count` over `range` whose arc `angle` falls in, or -1 outside
// the range. The range may cross straight up; the angle is taken modulo a
// turn from its start.
[[nodiscard]] inline int SlotAt(Range range, int count, float angle)
{
    if (count <= 0 || range.Width <= 0.0f)
        return -1;
    const float turn = 2.0f * std::numbers::pi_v<float>;
    float from = angle - range.Begin;
    while (from < 0.0f)
        from += turn;
    while (from >= turn)
        from -= turn;
    if (from >= range.Width)
        return -1;
    return std::min(static_cast<int>(std::floor(from / (range.Width / static_cast<float>(count)))), count - 1);
}

// The primary ring's range: the full circle, begun half a sector before
// straight up so sector 0 is centred there and the tools proceed clockwise
// in registry order like a clock.
[[nodiscard]] inline Range PrimaryRange(int count)
{
    if (count <= 0)
        return {};
    const float turn = 2.0f * std::numbers::pi_v<float>;
    return { -turn / static_cast<float>(count) * 0.5f, turn };
}

// The angular span of sector `index`.
[[nodiscard]] inline Span SectorSpan(int index, int count)
{
    return SlotSpan(PrimaryRange(count), index, count);
}

// The fan of `count` variants of `parent`: a standard wedge each, never
// stretched to fill more, centred on the parent's direction so the parent's
// petal reads as the root, and only as wide as its children need. Many
// variants share a turn evenly rather than overlap.
[[nodiscard]] inline Range FanRange(const Layout& layout, int parent, int count)
{
    if (count <= 0 || parent < 0)
        return {};
    const float turn = 2.0f * std::numbers::pi_v<float>;
    const float width = std::min(kVariantWidth, turn / static_cast<float>(count)) * static_cast<float>(count);
    const Span sector = SectorSpan(parent, layout.Count);
    return { (sector.Begin + sector.End) * 0.5f - width * 0.5f, width };
}

// A wheel of `count` slots opened at `pointer`, shifted -- never clipped -- so
// the whole of it (both rings, slots, caption) lies inside the frame. A frame
// too small on an axis centres the wheel on that axis instead.
[[nodiscard]] inline Layout Place(const Frame& frame, ImVec2 pointer, int count, int maxVariants = 0)
{
    Layout layout;
    layout.Radius = kRadius * frame.Scale;
    layout.Hub = kHub * frame.Scale;
    layout.Button = kButton * frame.Scale;
    layout.Seam = kSeam * frame.Scale;
    layout.Rim = kRim * frame.Scale;
    layout.CaptionGap = kCaptionGap * frame.Scale;
    layout.CaptionHeight = kCaptionHeight * frame.Scale;
    layout.Count = std::max(count, 0);
    layout.MaxVariants = std::max(maxVariants, 0);
    if (layout.MaxVariants > 0)
        layout.OuterRadius = layout.RimOuter() + layout.Seam + layout.Button * 0.5f;

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

// The direction of `point` from the wheel's centre, clockwise from straight up.
[[nodiscard]] inline float AngleAt(const Layout& layout, ImVec2 point)
{
    return std::atan2(point.x - layout.Center.x, -(point.y - layout.Center.y));
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
    return SlotAt(PrimaryRange(layout.Count), layout.Count, AngleAt(layout, point));
}

// The angular span of variant `index` of `parent`'s fan of `count`.
[[nodiscard]] inline Span VariantSpan(const Layout& layout, int parent, int index, int count)
{
    return SlotSpan(FanRange(layout, parent, count), index, count);
}

// The centre of variant `index`'s slot, on the outer ring.
[[nodiscard]] inline ImVec2 VariantSlotCenter(const Layout& layout, int parent, int index, int count)
{
    const Span span = VariantSpan(layout, parent, index, count);
    const float angle = (span.Begin + span.End) * 0.5f;
    return { layout.Center.x + std::sin(angle) * layout.OuterRadius, layout.Center.y - std::cos(angle) * layout.OuterRadius };
}

// Which band of the wheel `point` is in, by radius alone. Outer runs to the
// edge of the screen, as a sector does, so overshooting loses nothing; it
// does not exist for a wheel without variants.
enum class Ring : std::uint8_t
{
    Hub,
    Primary,
    Outer,
};

[[nodiscard]] inline Ring RingAt(const Layout& layout, ImVec2 point)
{
    const float dx = point.x - layout.Center.x;
    const float dy = point.y - layout.Center.y;
    const float r2 = dx * dx + dy * dy;
    if (r2 < layout.Hub * layout.Hub)
        return Ring::Hub;
    if (layout.MaxVariants <= 0 || r2 < layout.RimOuter() * layout.RimOuter())
        return Ring::Primary;
    return Ring::Outer;
}

// The variant of `parent`'s fan whose arc `point`'s direction falls in, or -1
// outside the fan. Direction only: the caller has already settled the ring.
[[nodiscard]] inline int VariantAt(const Layout& layout, int parent, int count, ImVec2 point)
{
    if (count <= 0 || parent < 0 || layout.Count <= 0)
        return -1;
    return SlotAt(FanRange(layout, parent, count), count, AngleAt(layout, point));
}
} // namespace ToolWheel
