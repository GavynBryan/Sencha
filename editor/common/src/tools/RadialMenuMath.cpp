#include "tools/RadialMenuMath.h"

#include <limits>
#include <utility>

namespace RadialMenu
{
namespace
{
constexpr float kTurn = 2.0f * std::numbers::pi_v<float>;
constexpr float kNever = std::numeric_limits<float>::infinity();

// How far along `heading` a ray from `origin`, inside the circle of `radius`
// about the origin of both, leaves it.
float LeaveCircle(Vec2d origin, Vec2d heading, float radius)
{
    const float a = heading.Dot(heading);
    const float b = origin.Dot(heading);
    const float c = origin.Dot(origin) - radius * radius;
    return (-b + std::sqrt(std::max(b * b - a * c, 0.0f))) / a;
}

// How far along `heading` a ray from `origin` first comes within `radius` of `point`.
float RayReachesDisc(Vec2d origin, Vec2d heading, Vec2d point, float radius)
{
    const Vec2d toPoint = point - origin;
    const float a = heading.Dot(heading);
    const float b = toPoint.Dot(heading);
    const float c = toPoint.Dot(toPoint) - radius * radius;
    if (c <= 0.0f)
        return 0.0f;
    const float discriminant = b * b - a * c;
    if (b <= 0.0f || discriminant < 0.0f)
        return kNever;
    return (b - std::sqrt(discriminant)) / a;
}

float FirstObstacleAlong(const Layout& layout, int parent, Vec2d origin, Vec2d heading)
{
    float nearest = RayReachesDisc(origin, heading, layout.Center, layout.Hub);
    for (int entry = 0; entry < layout.Count; ++entry)
        if (entry != parent)
            nearest = std::min(nearest, RayReachesDisc(origin, heading, SlotCenter(layout, entry), layout.Button * 0.5f));
    return nearest;
}

// How far along `heading` a ray from `origin`, outside `parent`'s wedge, enters it inside the rim.
float RayEntersWedge(const Layout& layout, int parent, Vec2d origin, Vec2d heading)
{
    const Span span = SectorSpan(parent, layout.Count);
    const Vec2d at = origin - layout.Center;
    float nearest = kNever;
    // Edge normals point clockwise: the wedge lies along the first's and against the last's.
    for (const auto [edge, inward] : { std::pair{ span.Begin, 1.0f }, std::pair{ span.End, -1.0f } })
    {
        const Vec2d along{ std::sin(edge), -std::cos(edge) };
        const Vec2d normal{ std::cos(edge), std::sin(edge) };
        const float closing = heading.Dot(normal);
        if (closing * inward <= 0.0f)
            continue;
        const float t = -at.Dot(normal) / closing;
        const float out = (at + heading * t).Dot(along);
        if (t > 0.0f && out >= 0.0f && out <= layout.RimOuter())
            nearest = std::min(nearest, t);
    }
    return nearest;
}

// Short of the neighbouring slots' directions, a wedge's width either side of
// `entry`'s own: near the rim a sweep to a neighbour and a dash to a flank
// variant run the same way, and the neighbour's slot settles it.
bool ShortOfNeighbourSlots(const Layout& layout, int entry, Vec2d point)
{
    const Span own = SectorSpan(entry, layout.Count);
    const float off = std::remainder(AngleAt(layout, point) - (own.Begin + own.End) * 0.5f, kTurn);
    return std::fabs(off) < kTurn / static_cast<float>(layout.Count);
}
} // namespace

bool HeadsIntoFan(const Layout& layout, int parent, int count, Vec2d from, Vec2d to)
{
    const Vec2d heading = to - from;
    if (count <= 0 || parent < 0 || heading.Dot(heading) <= 0.0f || RingAt(layout, to) != Ring::Primary)
        return false;
    // Every slot and the hub lie inside the rim, so nothing can intervene past it.
    if (FirstObstacleAlong(layout, parent, to, heading) < kNever)
        return false;

    const Vec2d at = to - layout.Center;
    const float rim = AngleAt(layout, to + heading * LeaveCircle(at, heading, layout.RimOuter()));
    const float edge = AngleAt(layout, to + heading * LeaveCircle(at, heading, layout.OuterExtent()));
    // Along a line the direction from the centre turns one way, through less than half a turn.
    const float turned = std::remainder(edge - rim, kTurn);
    const Range crossed = turned >= 0.0f ? Range{ rim, turned } : Range{ edge, -turned };
    const Range fan = FanRange(layout, parent, count);
    return Covers(fan, rim) || Covers(crossed, fan.Begin);
}

bool HeadsIntoWedge(const Layout& layout, int parent, Vec2d from, Vec2d to)
{
    const Vec2d heading = to - from;
    if (parent < 0 || heading.Dot(heading) <= 0.0f || RingAt(layout, to) != Ring::Primary)
        return false;
    return RayEntersWedge(layout, parent, to, heading) < FirstObstacleAlong(layout, parent, to, heading);
}

Pick PickAt(const Layout& layout, std::span<const int> fans, int hot, Vec2d anchor, Vec2d pointer)
{
    const auto fanOf = [fans](int entry)
    { return entry >= 0 && entry < static_cast<int>(fans.size()) ? fans[static_cast<std::size_t>(entry)] : 0; };
    switch (RingAt(layout, pointer))
    {
    case Ring::Hub:
        return {};
    case Ring::Primary:
    {
        const int sector = SectorAt(layout, pointer);
        const bool homeward = hot >= 0 && ShortOfNeighbourSlots(layout, hot, pointer) &&
                              (HeadsIntoFan(layout, hot, fanOf(hot), anchor, pointer) ||
                               (sector != hot && HeadsIntoWedge(layout, hot, anchor, pointer)));
        if (homeward)
            return { hot, -1, Hold::Transit };
        return { sector, -1, sector >= 0 ? Hold::Wedge : Hold::None };
    }
    case Ring::Outer:
    {
        // Over its own fan the hot entry keeps the pointer; past the fan's edge direction picks.
        const bool overOwnFan = hot >= 0 && VariantAt(layout, hot, fanOf(hot), pointer) >= 0;
        const int entry = overOwnFan ? hot : SectorAt(layout, pointer);
        const int variant = VariantAt(layout, entry, fanOf(entry), pointer);
        return { entry, variant, variant >= 0 ? Hold::Variant : Hold::Wedge };
    }
    }
    return {};
}
} // namespace RadialMenu
