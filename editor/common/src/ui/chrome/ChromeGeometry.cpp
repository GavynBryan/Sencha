#include "ChromeGeometry.h"

#include <algorithm>

namespace EditorChrome
{
ChamferPoly ChamferOutline(ImVec2 mn, ImVec2 mx, float chamfer)
{
    ChamferPoly poly;
    const float w = mx.x - mn.x;
    const float h = mx.y - mn.y;
    if (w <= 0.0f || h <= 0.0f)
        return poly;

    const float c = std::clamp(chamfer, 0.0f, std::min(w, h) * 0.5f);
    if (c <= 0.0f)
    {
        poly.P[0] = ImVec2(mn.x, mn.y);
        poly.P[1] = ImVec2(mx.x, mn.y);
        poly.P[2] = ImVec2(mx.x, mx.y);
        poly.P[3] = ImVec2(mn.x, mx.y);
        poly.Count = 4;
        return poly;
    }

    poly.P[0] = ImVec2(mn.x + c, mn.y);
    poly.P[1] = ImVec2(mx.x - c, mn.y);
    poly.P[2] = ImVec2(mx.x, mn.y + c);
    poly.P[3] = ImVec2(mx.x, mx.y - c);
    poly.P[4] = ImVec2(mx.x - c, mx.y);
    poly.P[5] = ImVec2(mn.x + c, mx.y);
    poly.P[6] = ImVec2(mn.x, mx.y - c);
    poly.P[7] = ImVec2(mn.x, mn.y + c);
    poly.Count = 8;
    return poly;
}

FrameRects FrameLayout(ImVec2 mn, ImVec2 mx, const FrameSpec& spec)
{
    const float ring = spec.Border + spec.Recess;
    FrameRects rects;
    rects.WellMin = ImVec2(mn.x + ring, mn.y + ring);
    rects.WellMax = ImVec2(std::max(rects.WellMin.x, mx.x - ring), std::max(rects.WellMin.y, mx.y - ring));
    rects.RailMin = rects.WellMin;
    rects.RailMax = ImVec2(rects.WellMax.x, std::min(rects.WellMax.y, rects.WellMin.y + std::max(0.0f, spec.Rail)));
    return rects;
}

HeaderRegions LayoutHeader(ImVec2 mn, ImVec2 mx, float capWidth, float titleWidth,
                           float ornamentWidth, float controlWidth, float gap)
{
    HeaderRegions r;
    float left = mn.x;
    float right = mx.x;
    if (right <= left || mx.y <= mn.y)
        return r;

    // The control region is reserved before anything else is laid out.
    if (controlWidth > 0.0f && right - left >= controlWidth)
    {
        r.ControlMin = ImVec2(right - controlWidth, mn.y);
        r.ControlMax = ImVec2(right, mx.y);
        r.HasControl = true;
        right -= controlWidth + gap;
    }

    if (capWidth > 0.0f && right - left >= capWidth)
    {
        r.CapMin = ImVec2(left, mn.y);
        r.CapMax = ImVec2(left + capWidth, mx.y);
        r.HasCap = true;
        left += capWidth + gap;
    }

    if (titleWidth > 0.0f && right - left >= titleWidth)
    {
        r.TitleMin = ImVec2(left, mn.y);
        r.TitleMax = ImVec2(left + titleWidth, mx.y);
        r.HasTitle = true;
        left += titleWidth + gap;
    }

    if (ornamentWidth > 0.0f && right - left >= ornamentWidth)
    {
        r.OrnamentMin = ImVec2(right - ornamentWidth, mn.y);
        r.OrnamentMax = ImVec2(right, mx.y);
        r.HasOrnament = true;
        right -= ornamentWidth + gap;
    }

    // A line shorter than two gaps reads as a smudge, not a line.
    if (right - left >= gap * 2.0f)
    {
        r.LineMin = ImVec2(left, mn.y);
        r.LineMax = ImVec2(right, mx.y);
        r.HasLine = true;
    }
    return r;
}

OrnamentTier TierFor(ImVec2 size, float mediumMin, float largeMin)
{
    const float shorter = std::min(size.x, size.y);
    if (shorter >= largeMin)
        return OrnamentTier::Large;
    if (shorter >= mediumMin)
        return OrnamentTier::Medium;
    return OrnamentTier::Small;
}

float RailOrnamentWidth(OrnamentTier tier, float ventLength, float slashLength, float gap)
{
    if (tier != OrnamentTier::Large)
        return 0.0f;
    return ventLength + gap + slashLength + gap;
}

int LayoutOrnaments(const FrameRects& rects, OrnamentTier tier, float screwRadius,
                    float ventLength, float slashLength, float gap, std::span<OrnamentSlot> out)
{
    if (tier == OrnamentTier::Small || out.empty())
        return 0;

    int count = 0;
    const auto place = [&](OrnamentKind kind, ImVec2 mn, ImVec2 mx) {
        if (count < static_cast<int>(out.size()))
            out[static_cast<std::size_t>(count++)] = OrnamentSlot{ kind, mn, mx };
    };

    // Screws sit just inside the well's bottom corners, in the padding no
    // widget reaches. Two per side is the smallest well that can hold them
    // without the pair touching.
    const float screw = screwRadius * 2.0f;
    const float wellW = rects.WellMax.x - rects.WellMin.x;
    const float wellH = rects.WellMax.y - rects.WellMin.y;
    if (screwRadius > 0.0f && wellW >= (screw + gap) * 2.0f && wellH >= (screw + gap) * 2.0f)
    {
        const float y0 = rects.WellMax.y - gap - screw;
        const float y1 = rects.WellMax.y - gap;
        place(OrnamentKind::Screw, ImVec2(rects.WellMin.x + gap, y0), ImVec2(rects.WellMin.x + gap + screw, y1));
        place(OrnamentKind::Screw, ImVec2(rects.WellMax.x - gap - screw, y0), ImVec2(rects.WellMax.x - gap, y1));
    }

    if (tier != OrnamentTier::Large)
        return count;

    // The rail's right end carries a vent with a triple slash beside it, only
    // when the rail is tall enough to hold them and wide enough that the cap
    // and a line still fit to their left.
    const float railH = rects.RailMax.y - rects.RailMin.y;
    const float railW = rects.RailMax.x - rects.RailMin.x;
    const float needed = RailOrnamentWidth(tier, ventLength, slashLength, gap);
    if (railH > 0.0f && ventLength > 0.0f && railW >= needed + railH * 3.0f + gap * 3.0f)
    {
        const float ventMaxX = rects.RailMax.x - gap;
        const float ventMinX = ventMaxX - ventLength;
        place(OrnamentKind::Vent, ImVec2(ventMinX, rects.RailMin.y), ImVec2(ventMaxX, rects.RailMax.y));
        if (slashLength > 0.0f)
            place(OrnamentKind::TripleSlash, ImVec2(ventMinX - gap - slashLength, rects.RailMin.y),
                  ImVec2(ventMinX - gap, rects.RailMax.y));
    }
    return count;
}
} // namespace EditorChrome
