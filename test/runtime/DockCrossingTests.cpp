#include <gtest/gtest.h>

#include <zone/DockCrossing.h>
#include <zone/WorldConnectionComponents.h>

namespace
{

constexpr ZoneId ZoneA{ 0xa1 };
constexpr ZoneId ZoneB{ 0xa2 };
constexpr DockId Dock{ 0xd1 };

WorldPartitionIndex CrossingIndex(Vec3d normal = Vec3d{ 1, 0, 0 },
                                  Vec3d right = Vec3d{ 0, 0, 1 },
                                  Vec3d up = Vec3d{ 0, 1, 0 },
                                  uint32_t directions = DockDirectionBoth)
{
    DockEndpoint a{
        .Id = Dock,
        .OwnerZone = ZoneA,
        .OwnerGraph = GraphId{ 0xb1 },
        .OtherZone = ZoneB,
        .OtherGraph = GraphId{ 0xb1 },
        .Side = DockSide::A,
        .Origin = {},
        .Normal = normal,
        .Right = right,
        .Up = up,
        .HalfExtents = { 2, 3 },
        .OwnerArmBoundsLocal = { { -4, -4, -4 }, { 4, 4, 0 } },
        .Directions = directions,
    };
    DockEndpoint b = a;
    b.OwnerZone = ZoneB;
    b.OtherZone = ZoneA;
    b.Side = DockSide::B;
    b.Normal = -normal;
    b.Right = -right;

    WorldPartitionManifest manifest;
    ZoneHeader zoneA;
    zoneA.Id = ZoneA;
    zoneA.Docks.push_back(a);
    ZoneHeader zoneB;
    zoneB.Id = ZoneB;
    zoneB.Docks.push_back(b);
    manifest.Zones = { zoneA, zoneB };
    return WorldPartitionIndex::Build(manifest);
}

} // namespace

TEST(DockCrossing, SweptCrossingHandlesFastMovement)
{
    const WorldPartitionIndex index = CrossingIndex();
    ZoneFocusState state{ .Current = ZoneA, .PreviousPosition = { -20, 0, 0 } };

    const auto crossing = AdvanceZoneFocus(state, index, Vec3d{ 20, 0, 0 });

    ASSERT_TRUE(crossing.has_value());
    EXPECT_EQ(crossing->Dock, Dock);
    EXPECT_EQ(crossing->From, ZoneA);
    EXPECT_EQ(crossing->To, ZoneB);
    EXPECT_EQ(state.Current, ZoneB);
    EXPECT_NEAR(crossing->Position.X, 0.0f, 1e-5f);
}

TEST(DockCrossing, BoundedPlaneRejectsCrossingOutsideSurface)
{
    const WorldPartitionIndex index = CrossingIndex();
    ZoneFocusState state{ .Current = ZoneA, .PreviousPosition = { -2, 0, 10 } };

    EXPECT_FALSE(AdvanceZoneFocus(state, index, Vec3d{ 2, 0, 10 }).has_value());
    EXPECT_EQ(state.Current, ZoneA);
}

TEST(DockCrossing, DirectionFlagsRejectReverseOnlyEndpoint)
{
    const WorldPartitionIndex index = CrossingIndex(
        Vec3d{ 1, 0, 0 }, Vec3d{ 0, 0, 1 }, Vec3d{ 0, 1, 0 }, DockDirectionBToA);
    ZoneFocusState state{ .Current = ZoneA, .PreviousPosition = { -2, 0, 0 } };

    EXPECT_FALSE(AdvanceZoneFocus(state, index, Vec3d{ 2, 0, 0 }).has_value());
}

TEST(DockCrossing, ResidentGateRetainsSweepUntilDestinationIsReady)
{
    const WorldPartitionIndex index = CrossingIndex();
    ZoneFocusState state{ .Current = ZoneA, .PreviousPosition = { -2, 0, 0 } };
    const DockCrossingOptions blocked{ .RequireResidentDestination = true };

    EXPECT_FALSE(AdvanceZoneFocus(state, index, Vec3d{ 2, 0, 0 }, blocked).has_value());
    EXPECT_EQ(state.Current, ZoneA);
    const Vec3d retainedStart{ -2, 0, 0 };
    EXPECT_EQ(state.PreviousPosition, retainedStart);

    const ZoneId resident[] = { ZoneB };
    const DockCrossingOptions ready{
        .ResidentPhysicsZones = resident,
        .RequireResidentDestination = true,
    };
    const auto crossing = AdvanceZoneFocus(state, index, Vec3d{ 2, 0, 0 }, ready);
    ASSERT_TRUE(crossing.has_value());
    EXPECT_EQ(crossing->To, ZoneB);
}

TEST(DockCrossing, GatePhysicsControlsPassageWithoutChangingTopology)
{
    const WorldPartitionIndex index = CrossingIndex();
    const DockGateBinding binding{ Dock };
    const ZoneId resident[] = { ZoneB };
    const DockCrossingOptions ready{
        .ResidentPhysicsZones = resident,
        .RequireResidentDestination = true,
    };
    ZoneFocusState state{ .Current = ZoneA, .PreviousPosition = { -1, 0, 0 } };

    const bool doorClosed = true;
    const Vec3d blockedPosition = doorClosed ? Vec3d{ -0.1f, 0, 0 }
                                             : Vec3d{ 1, 0, 0 };
    EXPECT_FALSE(AdvanceZoneFocus(state, index, blockedPosition, ready).has_value());
    EXPECT_EQ(state.Current, ZoneA);
    EXPECT_EQ(binding.Id, index.DocksFrom(ZoneA).front().Id);

    const auto crossing = AdvanceZoneFocus(state, index, Vec3d{ 1, 0, 0 }, ready);
    ASSERT_TRUE(crossing.has_value());
    EXPECT_EQ(crossing->To, ZoneB);
    EXPECT_EQ(binding.Id, crossing->Dock);
}

TEST(DockCrossing, HorizontalDockUsesItsAuthoredBasis)
{
    const WorldPartitionIndex index = CrossingIndex(
        Vec3d{ 0, 1, 0 }, Vec3d{ 1, 0, 0 }, Vec3d{ 0, 0, 1 });
    ZoneFocusState state{ .Current = ZoneA, .PreviousPosition = { 0, -2, 0 } };

    const auto crossing = AdvanceZoneFocus(state, index, Vec3d{ 0, 2, 0 });
    ASSERT_TRUE(crossing.has_value());
    EXPECT_EQ(crossing->To, ZoneB);
}

TEST(DockCrossing, DiagonalDockUsesItsAuthoredBasis)
{
    constexpr float diagonal = 0.70710678f;
    const Vec3d normal{ diagonal, 0, diagonal };
    const WorldPartitionIndex index = CrossingIndex(
        normal, Vec3d{ diagonal, 0, -diagonal }, Vec3d{ 0, 1, 0 });
    ZoneFocusState state{ .Current = ZoneA, .PreviousPosition = -normal * 2.0f };

    const auto crossing = AdvanceZoneFocus(state, index, normal * 2.0f);
    ASSERT_TRUE(crossing.has_value());
    EXPECT_EQ(crossing->To, ZoneB);
}

TEST(DockCrossing, ArmedDockSuppressesThresholdJitter)
{
    const WorldPartitionIndex index = CrossingIndex();
    ZoneFocusState state{ .Current = ZoneA, .PreviousPosition = { -1, 0, 0 } };
    ASSERT_TRUE(AdvanceZoneFocus(state, index, Vec3d{ 1, 0, 0 }).has_value());

    EXPECT_FALSE(AdvanceZoneFocus(state, index, Vec3d{ -0.1f, 0, 0 }).has_value());
    EXPECT_EQ(state.Current, ZoneB);
    EXPECT_EQ(state.ArmedDock, Dock);
}

TEST(DockCrossing, LeavingArmRearmsDockForReversal)
{
    const WorldPartitionIndex index = CrossingIndex();
    ZoneFocusState state{ .Current = ZoneA, .PreviousPosition = { -1, 0, 0 } };
    ASSERT_TRUE(AdvanceZoneFocus(state, index, Vec3d{ 1, 0, 0 }).has_value());

    EXPECT_FALSE(AdvanceZoneFocus(state, index, Vec3d{ 10, 0, 0 }).has_value());
    EXPECT_FALSE(state.ArmedDock.IsValid());

    const auto crossing = AdvanceZoneFocus(state, index, Vec3d{ -1, 0, 0 });
    ASSERT_TRUE(crossing.has_value());
    EXPECT_EQ(crossing->From, ZoneB);
    EXPECT_EQ(crossing->To, ZoneA);
}
