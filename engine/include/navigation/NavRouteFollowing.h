#pragma once

#include <math/Vec.h>

#include <cstdint>
#include <span>

// Path-following geometry as pure functions. World up is +Y and "planar" ignores
// Y. The rules are explained in docs/navigation/runtime.md, "Agents".

struct NavFollowTolerances
{
    // Planar metres within which a corner counts as reached.
    float CornerRadius = 0.3f;
    // Planar metres from the route beyond which the agent is off it.
    float Deviation = 1.5f;
    // Vertical metres between agent and route that still count as on it.
    float Height = 1.8f;
};

[[nodiscard]] float NavPlanarDistance(const Vec3d& a, const Vec3d& b);

// The unit planar direction from `from` toward `to`, or zero when they coincide.
[[nodiscard]] Vec3d NavPlanarDirection(const Vec3d& from, const Vec3d& to);

struct NavSegmentGap
{
    float Planar = 0.0f;
    float Vertical = 0.0f;
};
[[nodiscard]] NavSegmentGap NavGapToSegment(const Vec3d& point, const Vec3d& from, const Vec3d& to);

struct NavWalkAdvance
{
    std::uint32_t Corner = 0;
    // Off the segment being followed; Corner did not move.
    bool Deviated = false;
};

// Passes the walk leg's interior corners that `position` has reached or, near
// the route, overshot. The last corner is never passed: reaching it is the
// caller's rule.
[[nodiscard]] NavWalkAdvance NavAdvanceAlongWalk(std::span<const Vec3d> corners,
                                                 const Vec3d& legStart,
                                                 std::uint32_t corner,
                                                 const Vec3d& position,
                                                 const NavFollowTolerances& tolerances);

[[nodiscard]] bool NavHasArrived(const Vec3d& position, const Vec3d& goal, float radius, float height);

// The final leg's wish magnitude in [0, 1]: full until friction (per second)
// needs the remaining distance to stop the agent, then shrinking with it.
[[nodiscard]] float NavArrivalWishScale(float distance, float planarSpeed, float friction, float arriveRadius);

[[nodiscard]] float NavRemainingOnLeg(std::span<const Vec3d> corners, std::uint32_t corner, const Vec3d& position);

struct NavStuckWindow
{
    float Elapsed = 0.0f;
    float Expected = 0.0f;
    float StartRemaining = 0.0f;
    bool Open = false;
};

struct NavStuckRule
{
    // Agent seconds per window.
    float WindowSeconds = 1.0f;
    // A window that makes less than this fraction of its expected progress is stuck.
    float MinimumProgress = 0.25f;
};

// One tick of the agent's own time. A tick that does not count (carried, or
// asking for nothing) restarts the window. True when a window closes short.
[[nodiscard]] bool NavObserveStuck(NavStuckWindow& window, float remaining, float expected, float agentSeconds,
                                   bool counts, const NavStuckRule& rule);
