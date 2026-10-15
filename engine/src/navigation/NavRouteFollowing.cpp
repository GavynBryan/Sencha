#include <navigation/NavRouteFollowing.h>

#include <algorithm>
#include <cmath>

namespace
{
    struct Planar
    {
        float X = 0.0f;
        float Z = 0.0f;
    };

    Planar Flat(const Vec3d& v) { return { v.X, v.Z }; }
    Planar Minus(Planar a, Planar b) { return { a.X - b.X, a.Z - b.Z }; }
    float Dot(Planar a, Planar b) { return a.X * b.X + a.Z * b.Z; }
    float Length(Planar a) { return std::sqrt(Dot(a, a)); }

    bool OnSegment(const Vec3d& position, const Vec3d& from, const Vec3d& to, const NavFollowTolerances& tolerances)
    {
        const NavSegmentGap gap = NavGapToSegment(position, from, to);
        return gap.Planar <= tolerances.Deviation && gap.Vertical <= tolerances.Height;
    }

    Planar Unit(Planar a)
    {
        const float length = Length(a);
        return length > 1e-6f ? Planar{ a.X / length, a.Z / length } : Planar{};
    }

    // The plane that splits the turn at `corner` evenly; a U-turn has no
    // bisector, so its plane faces the way the agent was going.
    Planar TurnNormal(const Vec3d& previous, const Vec3d& corner, const Vec3d& next)
    {
        const Planar in = Unit(Minus(Flat(corner), Flat(previous)));
        const Planar out = Unit(Minus(Flat(next), Flat(corner)));
        const Planar bisector = Unit(Planar{ in.X + out.X, in.Z + out.Z });
        return Length(bisector) > 0.0f ? bisector : in;
    }

    bool Reached(const Vec3d& position, const Vec3d& previous, const Vec3d& corner, const Vec3d& next,
                 const NavFollowTolerances& tolerances)
    {
        if (NavHasArrived(position, corner, tolerances.CornerRadius, tolerances.Height))
            return true;
        const bool pastPlane =
            Dot(Minus(Flat(position), Flat(corner)), TurnNormal(previous, corner, next)) >= 0.0f;
        return pastPlane && OnSegment(position, corner, next, tolerances);
    }
}

float NavPlanarDistance(const Vec3d& a, const Vec3d& b)
{
    return Length(Minus(Flat(a), Flat(b)));
}

NavSegmentGap NavGapToSegment(const Vec3d& point, const Vec3d& from, const Vec3d& to)
{
    const Planar along = Minus(Flat(to), Flat(from));
    const float lengthSquared = Dot(along, along);
    const float t = lengthSquared > 0.0f
        ? std::clamp(Dot(Minus(Flat(point), Flat(from)), along) / lengthSquared, 0.0f, 1.0f)
        : 0.0f;
    const Planar closest{ from.X + along.X * t, from.Z + along.Z * t };
    const float closestY = from.Y + (to.Y - from.Y) * t;
    return NavSegmentGap{ Length(Minus(Flat(point), closest)), std::abs(point.Y - closestY) };
}

NavWalkAdvance NavAdvanceAlongWalk(std::span<const Vec3d> corners, const Vec3d& legStart, std::uint32_t corner,
                                   const Vec3d& position, const NavFollowTolerances& tolerances)
{
    NavWalkAdvance advance{ corner, false };
    if (corners.empty() || corner >= corners.size())
        return advance;
    while (advance.Corner + 1 < corners.size()
           && Reached(position, advance.Corner == 0 ? legStart : corners[advance.Corner - 1],
                      corners[advance.Corner], corners[advance.Corner + 1], tolerances))
        ++advance.Corner;
    const Vec3d& previous = advance.Corner == 0 ? legStart : corners[advance.Corner - 1];
    if (!OnSegment(position, previous, corners[advance.Corner], tolerances))
        advance = NavWalkAdvance{ corner, true };
    return advance;
}

bool NavHasArrived(const Vec3d& position, const Vec3d& goal, float radius, float height)
{
    return NavPlanarDistance(position, goal) <= radius && std::abs(position.Y - goal.Y) <= height;
}

float NavArrivalWishScale(float distance, float planarSpeed, float friction, float arriveRadius)
{
    if (!(friction > 0.0f))
        return 1.0f;
    const float stopping = std::max(planarSpeed / friction, arriveRadius);
    return stopping > 0.0f ? std::clamp(distance / stopping, 0.0f, 1.0f) : 1.0f;
}

float NavRemainingOnLeg(std::span<const Vec3d> corners, std::uint32_t corner, const Vec3d& position)
{
    if (corner >= corners.size())
        return 0.0f;
    float remaining = NavPlanarDistance(position, corners[corner]);
    for (std::size_t i = corner + 1; i < corners.size(); ++i)
        remaining += NavPlanarDistance(corners[i - 1], corners[i]);
    return remaining;
}

bool NavObserveStuck(NavStuckWindow& window, float remaining, float expected, float agentSeconds, bool counts,
                     const NavStuckRule& rule)
{
    if (!counts || !(expected > 0.0f))
    {
        window = NavStuckWindow{};
        return false;
    }
    if (!window.Open)
        window = NavStuckWindow{ 0.0f, 0.0f, remaining, true };
    window.Elapsed += agentSeconds;
    window.Expected += expected;
    if (window.Elapsed < rule.WindowSeconds)
        return false;
    const bool stuck = window.StartRemaining - remaining < rule.MinimumProgress * window.Expected;
    window = NavStuckWindow{ 0.0f, 0.0f, remaining, true };
    return stuck;
}
