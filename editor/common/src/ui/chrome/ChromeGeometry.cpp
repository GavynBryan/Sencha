#include "ChromeGeometry.h"

#include <algorithm>
#include <cmath>

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

BarRects BarLayout(ImVec2 mn, ImVec2 mx, const BarSpec& spec, float itemHeight)
{
    BarRects r;
    const float w = mx.x - mn.x;
    const float h = mx.y - mn.y;
    if (w <= 0.0f || h <= 0.0f)
        return r;

    // Two rims must leave the channel between them, so each is at most half the
    // band; a bar squeezed that far keeps its rims and loses its channel.
    const float rim = std::clamp(spec.Rim, 0.0f, h * 0.5f);
    r.TopRimMin = mn;
    r.TopRimMax = ImVec2(mx.x, mn.y + rim);
    r.BottomRimMin = ImVec2(mn.x, mx.y - rim);
    r.BottomRimMax = mx;

    // Caps terminate the channel, so they are only worth cutting when the
    // channel they leave is wider than the pair of them.
    const float cap = std::max(0.0f, spec.Cap);
    r.HasCaps = cap > 0.0f && w >= cap * 4.0f;
    const float inset = r.HasCaps ? cap : 0.0f;
    const float top = r.TopRimMax.y;
    const float bottom = r.BottomRimMin.y;
    r.LeftCapMin = ImVec2(mn.x, top);
    r.LeftCapMax = ImVec2(mn.x + inset, bottom);
    r.RightCapMin = ImVec2(mx.x - inset, top);
    r.RightCapMax = ImVec2(mx.x, bottom);

    r.ChannelMin = ImVec2(mn.x + inset, top);
    r.ChannelMax = ImVec2(mx.x - inset, std::max(top, bottom));

    const float channelH = r.ChannelMax.y - r.ChannelMin.y;
    const float laneH = std::clamp(itemHeight, 0.0f, channelH);
    // Whole pixels: the lane is flanked by 1px bevels that blur off-grid.
    const float laneTop = std::floor(r.ChannelMin.y + (channelH - laneH) * 0.5f);
    r.LaneMin = ImVec2(r.ChannelMin.x, laneTop);
    r.LaneMax = ImVec2(r.ChannelMax.x, laneTop + laneH);
    return r;
}

float BarHeightFor(const BarSpec& spec, float itemHeight)
{
    return std::max(0.0f, itemHeight) + (std::max(0.0f, spec.Rim) + std::max(0.0f, spec.Clearance)) * 2.0f;
}

BarRowRects BarRowLayout(float channelMin, float channelMax, float leftWidth, float centerWidth,
                         float rightWidth, float gap)
{
    BarRowRects r;
    leftWidth = std::max(0.0f, leftWidth);
    centerWidth = std::max(0.0f, centerWidth);
    rightWidth = std::max(0.0f, rightWidth);
    gap = std::max(0.0f, gap);

    r.LeftMin = channelMin;
    r.LeftMax = channelMin + leftWidth;
    r.RightMax = channelMax;
    r.RightMin = channelMax - rightWidth;

    // The midpoint of the channel is the bar's own center: the caps that bound
    // it are the same width at both ends, so centering here centers on screen.
    const float ideal = (channelMin + channelMax - centerWidth) * 0.5f;
    // The center block may not touch either outer block, nor leave the channel.
    const float low = std::max(channelMin, leftWidth > 0.0f ? r.LeftMax + gap : channelMin);
    const float high = std::min(channelMax, rightWidth > 0.0f ? r.RightMin - gap : channelMax) - centerWidth;

    if (low <= high)
    {
        r.CenterMin = std::clamp(ideal, low, high);
        r.Fit = r.CenterMin == ideal ? BarRowFit::Centered : BarRowFit::Shifted;
    }
    else
    {
        // Nothing fits between the outer blocks. Both of those keep their ends,
        // since a window's controls have to stay where a user reaches for them,
        // so the center starts after the left block and is allowed to run under
        // the right one. The caller is expected to drop something and lay out
        // again rather than ship the overlap.
        r.CenterMin = low;
        r.Fit = BarRowFit::Flowed;
    }
    r.CenterMax = r.CenterMin + centerWidth;

    r.LeftFreeMin = r.LeftMax;
    r.LeftFreeMax = std::max(r.LeftMax, r.CenterMin);
    r.RightFreeMin = r.CenterMax;
    r.RightFreeMax = std::max(r.CenterMax, r.RightMin);
    return r;
}

ImVec2 SurfaceTileUv(ImVec2 region, ImVec2 texture)
{
    return ImVec2(texture.x > 0.0f ? region.x / texture.x : 1.0f,
                  texture.y > 0.0f ? region.y / texture.y : 1.0f);
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
int LayoutRingOrnaments(ImVec2 mn, ImVec2 mx, float ring, float chamfer, float boltRadius,
                        float ventLength, float stripLength, float gap, std::span<OrnamentSlot> out)
{
    if (out.empty() || ring <= 0.0f)
        return 0;
    const float w = mx.x - mn.x;
    const float h = mx.y - mn.y;
    const float r = std::min(boltRadius, ring * 0.5f);
    // The same guard the chassis uses: a frame that cannot hold a bolt past
    // each chamfer has no ring worth mounting anything on.
    if (r < 1.0f || w < chamfer * 4.0f || h < chamfer * 4.0f)
        return 0;

    int count = 0;
    const auto place = [&](OrnamentKind kind, ImVec2 slotMin, ImVec2 slotMax) {
        if (count < static_cast<int>(out.size()))
            out[static_cast<std::size_t>(count++)] = OrnamentSlot{ kind, slotMin, slotMax };
    };

    const float along = chamfer + r * 2.0f;
    const float mid = ring * 0.5f;
    const auto bolt = [&](float cx, float cy) {
        place(OrnamentKind::Bolt, ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r));
    };
    bolt(mn.x + along, mn.y + mid);
    bolt(mx.x - along, mn.y + mid);
    bolt(mn.x + along, mx.y - mid);
    bolt(mx.x - along, mx.y - mid);

    // The clear run along a ring between its two bolts.
    const float runMin = mn.x + along + r + gap;
    const float runMax = mx.x - along - r - gap;
    const float run = runMax - runMin;

    if (stripLength > 0.0f && run >= stripLength * 2.0f + gap * 3.0f)
    {
        const float stripH = std::max(1.0f, ring * 0.35f);
        const float y0 = mx.y - mid - stripH * 0.5f;
        place(OrnamentKind::LightStrip, ImVec2(runMin, y0), ImVec2(runMin + stripLength, y0 + stripH));
        place(OrnamentKind::LightStrip, ImVec2(runMax - stripLength, y0), ImVec2(runMax, y0 + stripH));
    }

    if (ventLength > 0.0f && run >= ventLength + gap * 2.0f)
    {
        const float cx = (runMin + runMax) * 0.5f;
        const float inset = std::min(gap * 0.5f, ring * 0.25f);
        place(OrnamentKind::Vent, ImVec2(cx - ventLength * 0.5f, mn.y + inset),
              ImVec2(cx + ventLength * 0.5f, mn.y + ring - inset));
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

ReadoutRects ReadoutLayout(ImVec2 mn, float labelWidth, float valueWidth, float height,
                           float padding, float gap, float ledSize)
{
    padding = std::max(0.0f, padding);
    gap = std::max(0.0f, gap);
    height = std::max(0.0f, height);
    ledSize = std::clamp(ledSize, 0.0f, height);
    ReadoutRects r;
    r.HasLed = ledSize > 0.0f;
    r.LedMin = ImVec2(mn.x + padding, mn.y + (height - ledSize) * 0.5f);
    r.LedMax = ImVec2(r.LedMin.x + ledSize, r.LedMin.y + ledSize);
    r.LabelMin = ImVec2(mn.x + padding + (r.HasLed ? ledSize + gap : 0.0f), mn.y);
    r.ValueMin = ImVec2(r.LabelMin.x + std::max(0.0f, labelWidth) + gap, mn.y);
    r.Size = ImVec2(r.ValueMin.x + std::max(0.0f, valueWidth) + padding - mn.x, height);
    return r;
}

} // namespace EditorChrome
