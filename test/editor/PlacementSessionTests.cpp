#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/placement/PlacementResolver.h"
#include "document/placement/PlacementSession.h"
#include "viewport/GridSettings.h"

#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <cstring>

// The session holds the one pending placement and moves it through Idle,
// Aiming and Turning; whatever shows it and whatever commits it read the same
// value.
namespace
{
class PlacementSessionTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    LoggingProvider Logging;
    EditorDocument Document{ Logging };
    PlacementSession Session;
    const PlacementSubject Light{ .What = PlacementSubject::Kind::Entity, .Key = "PointLight" };
    const PlacementSubject Empty{ .What = PlacementSubject::Kind::Entity, .Key = {} };
};

[[nodiscard]] bool SameBits(const Transform3f& a, const Transform3f& b)
{
    return std::memcmp(&a, &b, sizeof(Transform3f)) == 0;
}
} // namespace

TEST_F(PlacementSessionTest, ItIsIdleUntilAimed)
{
    EXPECT_EQ(Session.Phase(), PlacementPhase::Idle);
    EXPECT_EQ(Session.Pending(), nullptr);
    EXPECT_FALSE(Session.BeginTurn());
    EXPECT_FALSE(Session.Commit().has_value());
}

TEST_F(PlacementSessionTest, AimingPendsThePlacementWithItsTransform)
{
    const PlacementSite site = UprightSite(Vec3d(1.0f, 0.0f, 2.0f));
    Session.Aim(Light, site, 1.0f, Document);
    ASSERT_EQ(Session.Phase(), PlacementPhase::Aiming);
    const PendingPlacement& pending = *Session.Pending();
    EXPECT_EQ(pending.Subject, Light);
    EXPECT_EQ(pending.Site, site);
    EXPECT_TRUE(SameBits(pending.Placement, PlacementTransform(site, 1.0f)));
    EXPECT_TRUE(pending.Refusal.empty());

    Session.Withdraw();
    EXPECT_EQ(Session.Phase(), PlacementPhase::Idle);
}

TEST_F(PlacementSessionTest, ACommitHandsBackExactlyWhatWasShown)
{
    Session.Aim(Empty, UprightSite(Vec3d(0.3f, 0.0f, -7.1f)), 2.2f, Document);
    const PendingPlacement shown = *Session.Pending();
    const std::optional<PendingPlacement> committed = Session.Commit();
    ASSERT_TRUE(committed.has_value());
    EXPECT_TRUE(SameBits(committed->Placement, shown.Placement));
    EXPECT_EQ(committed->Subject, shown.Subject);
    EXPECT_EQ(Session.Phase(), PlacementPhase::Idle);
}

TEST_F(PlacementSessionTest, ATurnHoldsTheSiteAndTheSubject)
{
    const PlacementSite site = UprightSite(Vec3d(4.0f, 0.0f, 0.0f));
    Session.Aim(Light, site, 0.0f, Document);
    ASSERT_TRUE(Session.BeginTurn());
    EXPECT_EQ(Session.Phase(), PlacementPhase::Turning);
    EXPECT_FALSE(Session.BeginTurn()) << "a turn does not begin twice";

    Session.Aim(Empty, UprightSite(Vec3d(9.0f, 0.0f, 9.0f)), 0.0f, Document);
    Session.Withdraw();
    EXPECT_EQ(Session.Phase(), PlacementPhase::Turning) << "aiming and withdrawing do not reach a turn";
    EXPECT_EQ(Session.Pending()->Site, site);
    EXPECT_EQ(Session.Pending()->Subject, Light);

    Session.Turn(1.5f, Vec3d(4.0f, 0.0f, 3.0f));
    EXPECT_FLOAT_EQ(Session.Pending()->Yaw, 1.5f);
    EXPECT_TRUE(SameBits(Session.Pending()->Placement, PlacementTransform(site, 1.5f)));
    ASSERT_TRUE(Session.TurnToward().has_value());

    const std::optional<PendingPlacement> committed = Session.Commit();
    ASSERT_TRUE(committed.has_value());
    EXPECT_FLOAT_EQ(committed->Yaw, 1.5f);
    EXPECT_EQ(Session.Phase(), PlacementPhase::Idle);
    EXPECT_FALSE(Session.TurnToward().has_value());
}

TEST_F(PlacementSessionTest, CancellingATurnReturnsToTheYawItBeganFrom)
{
    Session.Aim(Light, UprightSite(Vec3d{}), 0.75f, Document);
    ASSERT_TRUE(Session.BeginTurn());
    Session.Turn(3.0f, Vec3d(1.0f, 0.0f, 0.0f));
    Session.CancelTurn();
    EXPECT_EQ(Session.Phase(), PlacementPhase::Aiming);
    EXPECT_FLOAT_EQ(Session.Pending()->Yaw, 0.75f);
    EXPECT_FALSE(Session.TurnToward().has_value());
}

TEST_F(PlacementSessionTest, TransitionsThatDoNotApplyDoNothing)
{
    Session.Turn(1.0f, std::nullopt);
    Session.CancelTurn();
    Session.Withdraw();
    EXPECT_EQ(Session.Phase(), PlacementPhase::Idle);

    Session.Aim(Light, UprightSite(Vec3d{}), 0.0f, Document);
    Session.Turn(2.0f, std::nullopt);
    Session.CancelTurn();
    EXPECT_FLOAT_EQ(Session.Pending()->Yaw, 0.0f) << "a turn not begun moves nothing";

    Session.Clear();
    EXPECT_EQ(Session.Phase(), PlacementPhase::Idle);
}

TEST_F(PlacementSessionTest, TheDocumentJudgesASubjectWhenItArrives)
{
    Session.Aim({ .What = PlacementSubject::Kind::SceneSource, .Key = "asset://props/nowhere.sscene" },
                UprightSite(Vec3d{}), 0.0f, Document);
    EXPECT_FALSE(Session.Pending()->Refusal.empty());

    Session.Aim({ .What = PlacementSubject::Kind::Entity, .Key = "NotAComponent" }, UprightSite(Vec3d{}), 0.0f,
                Document);
    EXPECT_FALSE(Session.Pending()->Refusal.empty());

    Session.Aim(Light, UprightSite(Vec3d{}), 0.0f, Document);
    EXPECT_TRUE(Session.Pending()->Refusal.empty());
}

TEST(PlacementResolver, ASurfaceUnderThePointerWinsOverTheGrid)
{
    const GridSettings grid;
    const std::optional<PlacementSite> onSurface =
        ResolvePlacementSite(SurfaceHit{ .Point = Vec3d(1.0f, 2.0f, 3.0f), .Normal = Vec3d(0.0f, 1.0f, 0.0f) },
                             Vec3d(5.0f, 0.0f, 5.0f), grid);
    ASSERT_TRUE(onSurface.has_value());
    EXPECT_EQ(onSurface->Point, Vec3d(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(onSurface->Up, Vec3d::Up()) << "placements stand upright on any surface";

    const std::optional<PlacementSite> onGrid = ResolvePlacementSite(std::nullopt, Vec3d(5.0f, 0.0f, 5.0f), grid);
    ASSERT_TRUE(onGrid.has_value());
    EXPECT_EQ(onGrid->Point, Vec3d(5.0f, 0.0f, 5.0f));

    EXPECT_FALSE(ResolvePlacementSite(std::nullopt, std::nullopt, grid).has_value());
}

TEST(PlacementResolver, OnASurfaceAboveTheGridItSnapsAcrossTheSurfaceAtItsHeight)
{
    GridSettings grid;
    grid.Spacing = 0.5f;
    const SurfaceHit top{ .Point = Vec3d(1.13f, 2.37f, -0.81f), .Normal = Vec3d(0.0f, 1.0f, 0.0f) };
    const std::optional<PlacementSite> snapped = ResolvePlacementSite(top, std::nullopt, grid);
    ASSERT_TRUE(snapped.has_value());
    EXPECT_NEAR(snapped->Point.X, 1.0f, 1e-5f);
    EXPECT_NEAR(snapped->Point.Y, 2.37f, 1e-5f) << "it stays on the surface";
    EXPECT_NEAR(snapped->Point.Z, -1.0f, 1e-5f);

    // On a wall, across the wall at its depth; with snapping off, where it was hit.
    const SurfaceHit wall{ .Point = Vec3d(4.2f, 1.13f, 0.62f), .Normal = Vec3d(-1.0f, 0.0f, 0.0f) };
    const std::optional<PlacementSite> onWall = ResolvePlacementSite(wall, std::nullopt, grid);
    ASSERT_TRUE(onWall.has_value());
    EXPECT_NEAR(onWall->Point.X, 4.2f, 1e-5f);
    EXPECT_NEAR(onWall->Point.Y, 1.0f, 1e-5f);
    EXPECT_NEAR(onWall->Point.Z, 0.5f, 1e-5f);

    grid.SnapEnabled = false;
    const std::optional<PlacementSite> free = ResolvePlacementSite(top, std::nullopt, grid);
    ASSERT_TRUE(free.has_value());
    EXPECT_NEAR(free->Point.X, 1.13f, 1e-5f);
    EXPECT_NEAR(free->Point.Z, -0.81f, 1e-5f);
}
