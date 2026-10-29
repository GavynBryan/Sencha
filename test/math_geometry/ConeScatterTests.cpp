#include <gtest/gtest.h>

#include <math/Angle.h>
#include <math/geometry/3d/ConeScatter.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

// A scatter is the seed's alone, uniform inside its cone and never outside it.
TEST(ConeScatter, StaysInsideItsConeAndIsTheSeedsAlone)
{
    const Vec3d direction = Vec3d(0.3f, -0.2f, -1.0f).Normalized();
    float widest = 0.0f;
    for (std::uint64_t seed = 1; seed <= 512; ++seed)
    {
        const Vec3d scattered = ScatterInCone(direction, 10.0f, seed);
        EXPECT_NEAR(scattered.Magnitude(), 1.0f, 1e-5f);
        const float angle = ToDegrees(std::acos(std::clamp(scattered.Dot(direction), -1.0f, 1.0f)));
        EXPECT_LE(angle, 10.0f + 1e-3f);
        widest = std::max(widest, angle);
        EXPECT_EQ(scattered, ScatterInCone(direction, 10.0f, seed)) << "the same seed scatters the same way";
    }
    EXPECT_GT(widest, 9.0f) << "it reaches the edge of its cone";
    EXPECT_NE(ScatterInCone(direction, 10.0f, 1), ScatterInCone(direction, 10.0f, 2));
}

TEST(ConeScatter, NoConeIsTheDirectionAndAWideOneStopsAtARightAngle)
{
    const Vec3d up = Vec3d::Up();
    EXPECT_EQ(ScatterInCone(up, 0.0f, 9), up) << "straight up has no right vector of its own";
    EXPECT_EQ(ScatterInCone(Vec3d::Forward(), -5.0f, 9), Vec3d::Forward());
    for (std::uint64_t seed = 1; seed <= 64; ++seed)
        EXPECT_GE(ScatterInCone(Vec3d::Forward(), 170.0f, seed).Dot(Vec3d::Forward()), -1e-5f);
}
