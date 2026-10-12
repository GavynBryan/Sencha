#include "document/placement/PlacementSite.h"
#include "document/placement/PlacementTurn.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

// A placement stands on a site and turns about its up axis; a turn faces it
// toward where the pointer's ray meets the site's plane, and holds wherever
// that point is ill-posed.
namespace
{
constexpr float kTolerance = 1e-4f;

void ExpectNear(Vec3d actual, Vec3d expected, const char* what)
{
    EXPECT_NEAR(actual.X, expected.X, kTolerance) << what;
    EXPECT_NEAR(actual.Y, expected.Y, kTolerance) << what;
    EXPECT_NEAR(actual.Z, expected.Z, kTolerance) << what;
}

[[nodiscard]] Vec3d FacingOf(const PlacementSite& site, float yaw)
{
    return PlacementTransform(site, yaw).Rotation.RotateVector(Vec3d::Forward());
}

[[nodiscard]] Ray3d Down(Vec3d from) { return Ray3d(from, Vec3d(0.0f, -1.0f, 0.0f)); }
} // namespace

TEST(PlacementSite, UprightAtYawZeroIsUnturnedAndStandsOnItsPoint)
{
    const PlacementSite site = UprightSite(Vec3d(2.0f, 1.0f, -3.0f));
    const Transform3f placement = PlacementTransform(site, 0.0f);
    EXPECT_EQ(placement.Position, site.Point);
    EXPECT_EQ(placement.Scale, Vec3d::One());
    EXPECT_TRUE(placement.Rotation.NearlyEquals(Quatf::Identity(), kTolerance));
}

TEST(PlacementSite, AYawTurnsAboutUpFromFacingTowardTheSide)
{
    const PlacementSite site = UprightSite(Vec3d{});
    const float quarter = std::numbers::pi_v<float> / 2.0f;
    ExpectNear(FacingOf(site, quarter), site.Side(), "a quarter turn faces the side");
    ExpectNear(PlacementTransform(site, quarter).Rotation.RotateVector(Vec3d::Up()), site.Up, "it stays upright");
}

TEST(PlacementSite, AFrameOnATiltedPlaneStandsAlongItsNormal)
{
    const Vec3d normal = Vec3d(0.3f, 1.0f, 0.2f).Normalized();
    const PlacementSite site = SiteOnPlane(Vec3d(1.0f, 2.0f, 3.0f), normal);
    EXPECT_NEAR(site.Facing.Dot(site.Up), 0.0f, kTolerance);
    EXPECT_NEAR(site.Facing.Magnitude(), 1.0f, kTolerance);
    for (const float yaw : { 0.0f, 1.0f, 2.5f, 4.0f })
    {
        const Transform3f placement = PlacementTransform(site, yaw);
        ExpectNear(placement.Rotation.RotateVector(Vec3d::Up()), normal, "local up follows the normal");
        EXPECT_NEAR(FacingOf(site, yaw).Dot(normal), 0.0f, kTolerance) << "it faces along the plane";
    }
}

TEST(PlacementSite, AFrameWhoseNormalRunsAlongForwardFacesRight)
{
    for (const Vec3d normal : { Vec3d(0.0f, 0.0f, 1.0f), Vec3d(0.0f, 0.0f, -1.0f) })
    {
        const PlacementSite site = SiteOnPlane(Vec3d{}, normal);
        ExpectNear(site.Facing, Vec3d::Right(), "the fallback reference");
    }
}

TEST(PlacementSite, AYawReadsTheSameHoweverItWasReached)
{
    const float turn = 2.0f * std::numbers::pi_v<float>;
    EXPECT_NEAR(NormalizedYaw(-0.5f), turn - 0.5f, kTolerance);
    EXPECT_NEAR(NormalizedYaw(turn + 0.25f), 0.25f, kTolerance);
    EXPECT_GE(NormalizedYaw(-1e-9f), 0.0f);
    EXPECT_LT(NormalizedYaw(-1e-9f), turn);
}

TEST(PlacementTurn, ItFacesWhereThePointerMeetsThePlane)
{
    const PlacementSite site = UprightSite(Vec3d(1.0f, 0.0f, 1.0f));
    const Vec3d eye(1.0f, 6.0f, 9.0f);
    for (const Vec3d target : { Vec3d(4.0f, 0.0f, 1.0f), Vec3d(-3.0f, 0.0f, 1.0f), Vec3d(1.0f, 0.0f, 5.0f),
                                Vec3d(1.0f, 0.0f, -2.0f), Vec3d(3.0f, 0.0f, -1.0f), Vec3d(-2.0f, 0.0f, 3.5f) })
    {
        const std::optional<PlacementTurn::Aim> aim = PlacementTurn::FacingToward(site, Ray3d(eye, target - eye), 0.5f);
        ASSERT_TRUE(aim.has_value());
        ExpectNear(aim->Toward, target, "where the ray meets the plane");
        ExpectNear(FacingOf(site, aim->Yaw), (target - site.Point).Normalized(), "faces it");
    }
}

TEST(PlacementTurn, AnOrthographicViewLookingDownTurnsIt)
{
    const PlacementSite site = UprightSite(Vec3d{});
    const std::optional<PlacementTurn::Aim> aim = PlacementTurn::FacingToward(site, Down(Vec3d(0.0f, 50.0f, 3.0f)), 0.5f);
    ASSERT_TRUE(aim.has_value());
    ExpectNear(FacingOf(site, aim->Yaw), Vec3d(0.0f, 0.0f, 1.0f), "faces +Z");
}

TEST(PlacementTurn, ItHoldsWhereThePointIsIllPosed)
{
    const PlacementSite site = UprightSite(Vec3d{});
    // A front or side orthographic view: every ray lies along the floor.
    EXPECT_FALSE(PlacementTurn::FacingToward(site, Ray3d(Vec3d(3.0f, 1.0f, 10.0f), Vec3d(0.0f, 0.0f, -1.0f)), 0.5f).has_value());
    // Barely above the horizon in perspective: the floor is a grazing mile away.
    EXPECT_FALSE(PlacementTurn::FacingToward(site, Ray3d(Vec3d(0.0f, 1.0f, 0.0f), Vec3d(1.0f, -0.02f, 0.0f)), 0.5f).has_value());
    // Looking up from above the floor: the plane is behind the eye.
    EXPECT_FALSE(PlacementTurn::FacingToward(site, Ray3d(Vec3d(0.0f, 2.0f, 0.0f), Vec3d(1.0f, 1.0f, 0.0f)), 0.5f).has_value());
    // Too near the spot for the direction to mean anything.
    EXPECT_FALSE(PlacementTurn::FacingToward(site, Down(Vec3d(0.1f, 5.0f, 0.1f)), 0.5f).has_value());
    EXPECT_FALSE(PlacementTurn::FacingToward(site, Down(Vec3d(0.0f, 5.0f, 0.0f)), 0.0f).has_value());
}

TEST(PlacementTurn, ATiltedSiteTurnsWithinItsOwnPlane)
{
    const Vec3d normal = Vec3d(0.0f, 1.0f, 1.0f).Normalized();
    const PlacementSite site = SiteOnPlane(Vec3d{}, normal);
    const Vec3d target = site.Side() * 3.0f;
    const Vec3d eye = target + normal * 5.0f;
    const std::optional<PlacementTurn::Aim> aim = PlacementTurn::FacingToward(site, Ray3d(eye, target - eye), 0.5f);
    ASSERT_TRUE(aim.has_value());
    ExpectNear(FacingOf(site, aim->Yaw), site.Side(), "faces along the plane toward the pointer");
}
