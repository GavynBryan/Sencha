#include <navigation/NavRouteFollowing.h>

#include <algorithm>
#include <cmath>

namespace
{
    Vec3d ProjectOntoGround(const Vec3d& v) { return Vec3d(v.X, 0.0f, v.Z); }

    bool IsNearSegment(const Vec3d& position, const Vec3d& from, const Vec3d& to,
                       const NavFollowTolerances& tolerances)
    {
        const NavSegmentGap gap = NavGapToSegment(position, from, to);
        return gap.Planar <= tolerances.Deviation && gap.Vertical <= tolerances.Height;
    }

    // The plane that splits the turn at `corner` evenly; a U-turn has no
    // bisector, so its plane faces the way the agent was going.
    Vec3d TurnBisectorNormal(const Vec3d& previous, const Vec3d& corner, const Vec3d& next)
    {
        const Vec3d in = NavPlanarDirection(previous, corner);
        const Vec3d out = NavPlanarDirection(corner, next);
        const Vec3d bisector = NavPlanarDirection(Vec3d::Zero(), in + out);
        return bisector.SqrMagnitude() > 0.0f ? bisector : in;
    }

    bool HasReachedCorner(const Vec3d& position, const Vec3d& previous, const Vec3d& corner, const Vec3d& next,
                          const NavFollowTolerances& tolerances)
    {
        if (NavHasArrived(position, corner, tolerances.CornerRadius, tolerances.Height))
            return true;
        const bool pastPlane =
            ProjectOntoGround(position - corner).Dot(TurnBisectorNormal(previous, corner, next)) >= 0.0f;
        return pastPlane && IsNearSegment(position, corner, next, tolerances);
    }
}

float NavPlanarDistance(const Vec3d& a, const Vec3d& b)
{
    return Vec3d::Distance(ProjectOntoGround(a), ProjectOntoGround(b));
}

Vec3d NavPlanarDirection(const Vec3d& from, const Vec3d& to)
{
    const Vec3d along = ProjectOntoGround(to - from);
    const float length = along.Magnitude();
    return length > 1e-4f ? along * (1.0f / length) : Vec3d::Zero();
}

NavSegmentGap NavGapToSegment(const Vec3d& point, const Vec3d& from, const Vec3d& to)
{
    const Vec3d along = ProjectOntoGround(to - from);
    const float lengthSquared = along.SqrMagnitude();
    const float t = lengthSquared > 0.0f
        ? std::clamp(ProjectOntoGround(point - from).Dot(along) / lengthSquared, 0.0f, 1.0f)
        : 0.0f;
    const Vec3d closest = from + (to - from) * t;
    return NavSegmentGap{ NavPlanarDistance(point, closest), std::abs(point.Y - closest.Y) };
}

NavWalkAdvance NavAdvanceAlongWalk(std::span<const Vec3d> corners, const Vec3d& legStart, std::uint32_t corner,
                                   const Vec3d& position, const NavFollowTolerances& tolerances)
{
    NavWalkAdvance advance{ corner, false };
    if (corners.empty() || corner >= corners.size())
        return advance;
    while (advance.Corner + 1 < corners.size()
           && HasReachedCorner(position, advance.Corner == 0 ? legStart : corners[advance.Corner - 1],
                               corners[advance.Corner], corners[advance.Corner + 1], tolerances))
        ++advance.Corner;
    const Vec3d& previous = advance.Corner == 0 ? legStart : corners[advance.Corner - 1];
    if (!IsNearSegment(position, previous, corners[advance.Corner], tolerances))
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
