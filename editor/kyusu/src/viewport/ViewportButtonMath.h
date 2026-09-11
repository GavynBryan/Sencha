#pragma once

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <span>

// Where a row of buttons pinned to some geometry lands on screen. Pure pixel
// math with no ImGui runtime and no projection, so the panel that draws the
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
    ImVec2 Min = {}; // top-left of the whole strip
    float Size = 0.0f;
    float Gap = 0.0f;
    int Count = 0;
    bool Visible = false;

    [[nodiscard]] ImVec2 MinOf(int index) const
    {
        return ImVec2(Min.x + static_cast<float>(index) * (Size + Gap), Min.y);
    }
    [[nodiscard]] ImVec2 MaxOf(int index) const
    {
        const ImVec2 min = MinOf(index);
        return ImVec2(min.x + Size, min.y + Size);
    }
    // Where to centre a caption describing buttons `first` through `last`: above
    // the span they occupy, so a readout placed here comes from the same layout
    // rule as the buttons it labels.
    [[nodiscard]] ImVec2 CaptionCenter(int first, int last) const
    {
        if (Count <= 0)
            return Min;
        const int lo = std::clamp(first, 0, Count - 1);
        const int hi = std::clamp(last, lo, Count - 1);
        return ImVec2((MinOf(lo).x + MaxOf(hi).x) * 0.5f, Min.y - Gap);
    }

    // The button under `point`, or -1.
    [[nodiscard]] int HitTest(ImVec2 point) const
    {
        for (int i = 0; Visible && i < Count; ++i)
        {
            const ImVec2 min = MinOf(i);
            const ImVec2 max = MaxOf(i);
            if (point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y)
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
[[nodiscard]] inline Row Layout(std::span<const std::optional<ImVec2>> anchors, int count, float scale,
                                ImVec2 regionMin, ImVec2 regionMax)
{
    Row row;
    if (count <= 0)
        return row;

    ImVec2 boundsMin(0.0f, 0.0f);
    ImVec2 boundsMax(0.0f, 0.0f);
    bool any = false;
    for (const std::optional<ImVec2>& anchor : anchors)
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
        boundsMin.x = std::min(boundsMin.x, anchor->x);
        boundsMin.y = std::min(boundsMin.y, anchor->y);
        boundsMax.x = std::max(boundsMax.x, anchor->x);
        boundsMax.y = std::max(boundsMax.y, anchor->y);
    }
    if (!any)
        return row;

    row.Size = kSize * scale;
    row.Gap = kGap * scale;
    row.Count = count;
    row.Visible = true;

    const float width = static_cast<float>(count) * row.Size + static_cast<float>(count - 1) * row.Gap;
    row.Min = ImVec2((boundsMin.x + boundsMax.x) * 0.5f - width * 0.5f,
                     boundsMin.y - row.Gap - row.Size);

    const float margin = kMargin * scale;
    const float lowX = regionMin.x + margin;
    const float highX = regionMax.x - margin - width;
    const float lowY = regionMin.y + margin;
    const float highY = regionMax.y - margin - row.Size;
    // A region too small to hold the strip pins it to the near edge rather than
    // inverting the clamp.
    row.Min.x = highX >= lowX ? std::clamp(row.Min.x, lowX, highX) : lowX;
    row.Min.y = highY >= lowY ? std::clamp(row.Min.y, lowY, highY) : lowY;
    return row;
}
} // namespace ViewportButtons
