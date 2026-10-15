#include <navigation/NavRouteFollowing.h>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace
{
    // An L: east 10 m, then north 10 m.
    const Vec3d kStart(0.0f, 0.0f, 0.0f);
    const std::array<Vec3d, 2> kElbow{ Vec3d(10.0f, 0.0f, 0.0f), Vec3d(10.0f, 0.0f, 10.0f) };

    struct AdvanceCase
    {
        const char* Name;
        Vec3d Position;
        std::uint32_t ExpectedCorner;
        bool ExpectedDeviated;
    };
}

TEST(NavRouteFollowing, AdvanceAlongAnElbow)
{
    const NavFollowTolerances tolerances{ .CornerRadius = 0.3f, .Deviation = 1.5f, .Height = 1.8f };
    const AdvanceCase cases[] = {
        { "on the first leg", Vec3d(5.0f, 0.0f, 0.2f), 0, false },
        { "a metre short of the corner, on the leg", Vec3d(9.0f, 0.0f, 0.0f), 0, false },
        { "inside the corner radius", Vec3d(9.8f, 0.0f, 0.1f), 1, false },
        { "overshot past the corner, still near the route", Vec3d(11.0f, 0.0f, 0.5f), 1, false },
        { "cut the corner close to the next leg", Vec3d(9.4f, 0.0f, 1.2f), 1, false },
        { "beside the first leg but off it", Vec3d(5.0f, 0.0f, 3.0f), 0, true },
        { "teleported past the corner plane, far from both legs", Vec3d(14.0f, 0.0f, 4.0f), 0, true },
        { "beyond the corner plane but outside the corridor", Vec3d(6.0f, 0.0f, 5.0f), 0, true },
        { "a floor above the route", Vec3d(5.0f, 4.0f, 0.0f), 0, true },
        { "on a gentle stair, within the height", Vec3d(5.0f, 1.2f, 0.0f), 0, false },
    };
    for (const AdvanceCase& c : cases)
    {
        const NavWalkAdvance advance = NavAdvanceAlongWalk(kElbow, kStart, 0, c.Position, tolerances);
        EXPECT_EQ(advance.Corner, c.ExpectedCorner) << c.Name;
        EXPECT_EQ(advance.Deviated, c.ExpectedDeviated) << c.Name;
    }
}

TEST(NavRouteFollowing, NeverPassesTheLegsLastCorner)
{
    const NavFollowTolerances tolerances;
    const NavWalkAdvance atEnd = NavAdvanceAlongWalk(kElbow, kStart, 1, Vec3d(10.0f, 0.0f, 10.0f), tolerances);
    EXPECT_EQ(atEnd.Corner, 1u);
    EXPECT_FALSE(atEnd.Deviated);
    const NavWalkAdvance past = NavAdvanceAlongWalk(kElbow, kStart, 1, Vec3d(10.0f, 0.0f, 11.0f), tolerances);
    EXPECT_EQ(past.Corner, 1u);
}

TEST(NavRouteFollowing, AFastAgentPassesSeveralCornersInOneTick)
{
    // A staircase of short legs; one 0.5 m step (30 m/s at 60 Hz) crosses all of them.
    const std::vector<Vec3d> corners{ Vec3d(1.0f, 0.0f, 0.0f), Vec3d(1.0f, 0.0f, 0.2f), Vec3d(1.2f, 0.0f, 0.2f),
                                      Vec3d(1.2f, 0.0f, 0.4f), Vec3d(8.0f, 0.0f, 0.4f) };
    const NavWalkAdvance advance =
        NavAdvanceAlongWalk(corners, kStart, 0, Vec3d(1.5f, 0.0f, 0.4f), NavFollowTolerances{});
    EXPECT_EQ(advance.Corner, 4u);
    EXPECT_FALSE(advance.Deviated);
}

TEST(NavRouteFollowing, ArrivalNeedsTheRadiusAndTheFloor)
{
    EXPECT_TRUE(NavHasArrived(Vec3d(0.2f, 0.9f, 0.0f), Vec3d(0.0f, 0.0f, 0.0f), 0.5f, 1.8f));
    EXPECT_FALSE(NavHasArrived(Vec3d(0.6f, 0.0f, 0.0f), Vec3d(0.0f, 0.0f, 0.0f), 0.5f, 1.8f));
    EXPECT_FALSE(NavHasArrived(Vec3d(0.0f, 3.0f, 0.0f), Vec3d(0.0f, 0.0f, 0.0f), 0.5f, 1.8f));
}

TEST(NavRouteFollowing, ArrivalBrakesWithinTheStoppingDistance)
{
    // 4.5 m/s against friction 6 stops in 0.75 m.
    EXPECT_FLOAT_EQ(NavArrivalWishScale(5.0f, 4.5f, 6.0f, 0.3f), 1.0f);
    EXPECT_FLOAT_EQ(NavArrivalWishScale(0.375f, 4.5f, 6.0f, 0.3f), 0.5f);
    EXPECT_FLOAT_EQ(NavArrivalWishScale(0.0f, 4.5f, 6.0f, 0.3f), 0.0f);
    // At rest, the arrive radius is the braking distance.
    EXPECT_FLOAT_EQ(NavArrivalWishScale(0.15f, 0.0f, 6.0f, 0.3f), 0.5f);
    // No friction: nothing to plan a stop around.
    EXPECT_FLOAT_EQ(NavArrivalWishScale(0.1f, 4.5f, 0.0f, 0.3f), 1.0f);
}

TEST(NavRouteFollowing, RemainingCountsTheRestOfTheLeg)
{
    EXPECT_FLOAT_EQ(NavRemainingOnLeg(kElbow, 0, Vec3d(4.0f, 0.0f, 0.0f)), 16.0f);
    EXPECT_FLOAT_EQ(NavRemainingOnLeg(kElbow, 1, Vec3d(10.0f, 0.0f, 4.0f)), 6.0f);
    EXPECT_FLOAT_EQ(NavRemainingOnLeg(kElbow, 2, Vec3d(10.0f, 0.0f, 4.0f)), 0.0f);
}

TEST(NavRouteFollowing, StuckWhenAWindowFallsShortOfItsProgress)
{
    const NavStuckRule rule{ .WindowSeconds = 1.0f, .MinimumProgress = 0.25f };
    NavStuckWindow window;
    // Asking for 4 m/s for a second and getting 0.5 m: stuck.
    bool stuck = false;
    float remaining = 20.0f;
    for (int tick = 0; tick < 15; ++tick)
    {
        remaining -= 0.05f;
        stuck = NavObserveStuck(window, remaining, 0.4f, 0.1f, true, rule) || stuck;
    }
    EXPECT_TRUE(stuck);

    // Getting 3 m of 4: moving.
    window = NavStuckWindow{};
    stuck = false;
    for (int tick = 0; tick < 15; ++tick)
    {
        remaining -= 0.3f;
        stuck = NavObserveStuck(window, remaining, 0.4f, 0.1f, true, rule) || stuck;
    }
    EXPECT_FALSE(stuck);
}

TEST(NavRouteFollowing, BeingCarriedOrHeldIsNeverStuck)
{
    const NavStuckRule rule{ .WindowSeconds = 1.0f, .MinimumProgress = 0.25f };
    NavStuckWindow window;
    bool stuck = false;
    for (int tick = 0; tick < 50; ++tick)
    {
        // Every other tick an ability holds the agent in place.
        const bool carried = tick % 2 == 0;
        stuck = NavObserveStuck(window, 20.0f, 0.4f, 0.1f, !carried, rule) || stuck;
    }
    EXPECT_FALSE(stuck);
}
