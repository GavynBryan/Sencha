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

ChamferPoly CornerWedge(ImVec2 mn, ImVec2 mx, float chamfer, float thickness)
{
    ChamferPoly poly;
    const float w = mx.x - mn.x;
    const float h = mx.y - mn.y;
    if (w <= 0.0f || h <= 0.0f || chamfer <= 0.0f || thickness <= 0.0f)
        return poly;
    const float half = std::min(w, h) * 0.5f;
    const float c = std::min(chamfer, half);
    // Offsetting a 45-degree cut inward by `thickness` moves its ends along
    // the edges by thickness * sqrt(2).
    const float reach = std::min(c + thickness * 1.41421356f, half);
    if (reach <= c)
        return poly;
    poly.P[0] = ImVec2(mn.x, mn.y + c);
    poly.P[1] = ImVec2(mn.x + c, mn.y);
    poly.P[2] = ImVec2(mn.x + reach, mn.y);
    poly.P[3] = ImVec2(mn.x, mn.y + reach);
    poly.Count = 4;
    return poly;
}

ChamferPoly SlantedCap(ImVec2 mn, ImVec2 mx, float lean)
{
    ChamferPoly poly;
    const float w = mx.x - mn.x;
    if (w <= 0.0f || mx.y <= mn.y)
        return poly;
    const float l = std::clamp(lean, 0.0f, w * 0.5f);
    poly.P[0] = ImVec2(mn.x + l, mn.y);
    poly.P[1] = ImVec2(mx.x, mn.y);
    poly.P[2] = ImVec2(mx.x - l, mx.y);
    poly.P[3] = ImVec2(mn.x, mx.y);
    poly.Count = 4;
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

float RailOrnamentWidth(OrnamentTier tier, float ventLength, float slashLength, float ledDiameter, float gap)
{
    if (tier == OrnamentTier::Small)
        return 0.0f;
    float width = ledDiameter > 0.0f ? ledDiameter + gap : 0.0f;
    if (tier == OrnamentTier::Large && ventLength > 0.0f)
        width += ventLength + gap + (slashLength > 0.0f ? slashLength + gap : 0.0f);
    return width;
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

    // The rail's right end carries the LED, and at Large a vent with a triple
    // slash left of it, only when the rail is tall enough to hold them and
    // wide enough that the cap and a line still fit to their left. A rail
    // that cannot hold the Large set keeps the LED alone.
    const float railH = rects.RailMax.y - rects.RailMin.y;
    const float railW = rects.RailMax.x - rects.RailMin.x;
    const float led = screwRadius * 2.0f;
    const auto fits = [&](OrnamentTier t) {
        return railW >= RailOrnamentWidth(t, ventLength, slashLength, led, gap) + railH * 3.0f + gap * 3.0f;
    };
    OrnamentTier railTier = tier;
    if (railTier == OrnamentTier::Large && !fits(railTier))
        railTier = OrnamentTier::Medium;
    if (railH <= 0.0f || !fits(railTier))
        return count;

    float right = rects.RailMax.x - gap;
    if (led > 0.0f)
    {
        const float d = std::min(led, railH);
        const float cy = (rects.RailMin.y + rects.RailMax.y) * 0.5f;
        place(OrnamentKind::StatusLed, ImVec2(right - d, cy - d * 0.5f), ImVec2(right, cy + d * 0.5f));
        right -= led + gap;
    }
    if (railTier == OrnamentTier::Large && ventLength > 0.0f)
    {
        place(OrnamentKind::Vent, ImVec2(right - ventLength, rects.RailMin.y), ImVec2(right, rects.RailMax.y));
        right -= ventLength + gap;
        if (slashLength > 0.0f)
            place(OrnamentKind::TripleSlash, ImVec2(right - slashLength, rects.RailMin.y),
                  ImVec2(right, rects.RailMax.y));
    }
    return count;
}
TileRects TileLayout(ImVec2 mn, float size, float labelHeight, float badgeSize, float inset)
{
    size = std::max(0.0f, size);
    labelHeight = std::max(0.0f, labelHeight);
    inset = std::clamp(inset, 0.0f, size * 0.5f);
    badgeSize = std::clamp(badgeSize, 0.0f, size - inset * 2.0f);
    const ImVec2 end(mn.x + size, mn.y + size);
    return { mn, end, ImVec2(mn.x, end.y), ImVec2(end.x, end.y + labelHeight),
             ImVec2(mn.x + inset, end.y - inset - badgeSize),
             ImVec2(mn.x + inset + badgeSize, end.y - inset), ImVec2(size, size + labelHeight) };
}

BracketLines BracketCorners(ImVec2 mn, ImVec2 mx, float length)
{
    BracketLines result;
    if (mx.x <= mn.x || mx.y <= mn.y || length <= 0.0f)
        return result;
    length = std::min(length, std::min(mx.x - mn.x, mx.y - mn.y) * 0.5f);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
        {
            const ImVec2 corner(x ? mx.x : mn.x, y ? mx.y : mn.y);
            result.Points[result.Count++] = ImVec2(corner.x + (x ? -length : length), corner.y);
            result.Points[result.Count++] = corner;
            result.Points[result.Count++] = corner;
            result.Points[result.Count++] = ImVec2(corner.x, corner.y + (y ? -length : length));
        }
    return result;
}

} // namespace EditorChrome
