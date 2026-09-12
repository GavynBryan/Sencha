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
// Two rings: the primary ring of tools, and outside its rim an optional ring
// of the hovered tool's variants, laid as that tool's sector continued
// outward and split evenly. The variant ring exists in the layout whenever
// any tool has variants (the wheel reserves room for it at open and never
// moves afterwards); which fan is showing is the session's business.
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
inline constexpr float kChildArc = 30.0f;      // the least arc a variant petal may have at its ring: a slot and its seams
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
    float ChildArc = 0.0f;
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
    layout.ChildArc = kChildArc * frame.Scale;
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

// The angular width of one petal in a fan of `count` variants: the parent's
// sector split evenly, widened only when a petal would have less than
// ChildArc of arc at its ring (a dense primary ring), and never past a full
// turn between them. So a fan always contains its parent's sector, and with
// a few tools it fills that sector exactly, flush with the parent's flanks.
[[nodiscard]] inline float VariantWidth(const Layout& layout, int count)
{
    if (count <= 0 || layout.Count <= 0)
        return 0.0f;
    const float turn = 2.0f * std::numbers::pi_v<float>;
    const float even = turn / static_cast<float>(layout.Count) / static_cast<float>(count);
    const float least = layout.OuterRadius > 0.0f ? layout.ChildArc / layout.OuterRadius : 0.0f;
    return std::min(std::max(even, least), turn / static_cast<float>(count));
}

// The angular span of variant `index` of `parent`'s fan of `count`, centred
// on the parent's sector like the parent's own petal continued outward.
[[nodiscard]] inline Span VariantSpan(const Layout& layout, int parent, int index, int count)
{
    if (count <= 0)
        return {};
    const Span sector = SectorSpan(parent, layout.Count);
    const float middle = (sector.Begin + sector.End) * 0.5f;
    const float width = VariantWidth(layout, count);
    const float begin = middle - width * static_cast<float>(count) * 0.5f + width * static_cast<float>(index);
    return { begin, begin + width };
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
    const float turn = 2.0f * std::numbers::pi_v<float>;
    const float dx = point.x - layout.Center.x;
    const float dy = point.y - layout.Center.y;
    float angle = std::atan2(dx, -dy) - VariantSpan(layout, parent, 0, count).Begin;
    while (angle < 0.0f)
        angle += turn;
    while (angle >= turn)
        angle -= turn;
    const int index = static_cast<int>(std::floor(angle / VariantWidth(layout, count)));
    return index < count ? index : -1;
}
} // namespace ToolWheel
