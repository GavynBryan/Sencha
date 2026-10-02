#pragma once

#include <math/Vec.h>

#include <algorithm>
#include <optional>
#include <span>

// Where a row of buttons pinned to some geometry lands on screen. Pure pixel
// math with no UI runtime and no projection, so the marks that draw the
// buttons and the tool that hit-tests them go through one rule rather than two
// versions of it that drift apart.
//
// Sizes are in design pixels and scaled at the call site, which is why the
// scale is a parameter: this header stays free of editor style state so it can
// be tested on its own.
namespace ViewportButtons
{
inline constexpr float kSize = 22.0f;  // one button, square
inline constexpr float kGap = 5.0f;    // between buttons, and above the geometry
inline constexpr float kMargin = 8.0f; // kept clear of the viewport edge

struct Row
{
    Vec2d Min = {}; // top-left of the whole strip
    float Size = 0.0f;
    float Gap = 0.0f;
    int Count = 0;
    bool Visible = false;

    [[nodiscard]] Vec2d MinOf(int index) const
    {
        return Vec2d(Min.X + static_cast<float>(index) * (Size + Gap), Min.Y);
    }
    [[nodiscard]] Vec2d MaxOf(int index) const
    {
        const Vec2d min = MinOf(index);
        return Vec2d(min.X + Size, min.Y + Size);
    }
    // Where to centre a caption describing buttons `first` through `last`: above
    // the span they occupy, so a readout placed here comes from the same layout
    // rule as the buttons it labels.
    [[nodiscard]] Vec2d CaptionCenter(int first, int last) const
    {
        if (Count <= 0)
            return Min;
        const int lo = std::clamp(first, 0, Count - 1);
        const int hi = std::clamp(last, lo, Count - 1);
        return Vec2d((MinOf(lo).X + MaxOf(hi).X) * 0.5f, Min.Y - Gap);
    }

    // The button under `point`, or -1.
    [[nodiscard]] int HitTest(Vec2d point) const
    {
        for (int i = 0; Visible && i < Count; ++i)
        {
            const Vec2d min = MinOf(i);
            const Vec2d max = MaxOf(i);
            if (point.X >= min.X && point.X <= max.X && point.Y >= min.Y && point.Y <= max.Y)
                return i;
        }
        return -1;
    }
};

// The row for `count` buttons sitting just above the screen bounds of
// `anchors`, centred on them, and nudged so the whole strip clears the region
// edges.
//
// An anchor with no pixel is behind the camera and contributes nothing; when
// none of them project, the row is hidden rather than clamped. Clamping a
// meaningless projection onto a screen edge would leave a live button somewhere
// the geometry is not.
[[nodiscard]] inline Row Layout(std::span<const std::optional<Vec2d>> anchors, int count, float scale,
                                Vec2d regionMin, Vec2d regionMax)
{
    Row row;
    if (count <= 0)
        return row;

    Vec2d boundsMin(0.0f, 0.0f);
    Vec2d boundsMax(0.0f, 0.0f);
    bool any = false;
    for (const std::optional<Vec2d>& anchor : anchors)
    {
        if (!anchor.has_value())
            continue;
        if (!any)
        {
            boundsMin = *anchor;
            boundsMax = *anchor;
            any = true;
            continue;
        }
        boundsMin.X = std::min(boundsMin.X, anchor->X);
        boundsMin.Y = std::min(boundsMin.Y, anchor->Y);
        boundsMax.X = std::max(boundsMax.X, anchor->X);
        boundsMax.Y = std::max(boundsMax.Y, anchor->Y);
    }
    if (!any)
        return row;

    row.Size = kSize * scale;
    row.Gap = kGap * scale;
    row.Count = count;
    row.Visible = true;

    const float width = static_cast<float>(count) * row.Size + static_cast<float>(count - 1) * row.Gap;
    row.Min = Vec2d((boundsMin.X + boundsMax.X) * 0.5f - width * 0.5f,
                     boundsMin.Y - row.Gap - row.Size);

    const float margin = kMargin * scale;
    const float lowX = regionMin.X + margin;
    const float highX = regionMax.X - margin - width;
    const float lowY = regionMin.Y + margin;
    const float highY = regionMax.Y - margin - row.Size;
    // A region too small to hold the strip pins it to the near edge rather than
    // inverting the clamp.
    row.Min.X = highX >= lowX ? std::clamp(row.Min.X, lowX, highX) : lowX;
    row.Min.Y = highY >= lowY ? std::clamp(row.Min.Y, lowY, highY) : lowY;
    return row;
}
} // namespace ViewportButtons
