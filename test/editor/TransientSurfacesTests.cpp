#include "shell/TransientSurfaces.h"

#include <gtest/gtest.h>

#include <vector>

// Escape takes away one thing at a time, the closest first, and says when there
// is nothing left so the workspace's own steps can run. Presses elsewhere take
// away only what is meant to go lightly.

TEST(TransientSurfaces, EscapeTakesTheHighestPriorityThenTheNewest)
{
    TransientSurfaces open;
    const auto drawer = open.Open({ .Owner = "level", .Dismiss = TransientDismiss::Explicit, .Priority = 1 });
    const auto popover = open.Open({ .Owner = "shell", .Dismiss = TransientDismiss::Light, .Priority = 2 });
    const auto tray = open.Open({ .Owner = "level", .Dismiss = TransientDismiss::Explicit, .Priority = 1 });

    EXPECT_TRUE(open.Escape());
    EXPECT_FALSE(open.IsOpen(popover));
    EXPECT_TRUE(open.Escape());
    EXPECT_FALSE(open.IsOpen(tray)) << "among equals, the newer one goes first";
    EXPECT_TRUE(open.IsOpen(drawer));
    EXPECT_TRUE(open.Escape());
    EXPECT_FALSE(open.Escape()) << "nothing left: the workspace's escape steps run next";
}

TEST(TransientSurfaces, APinnedOneStaysThroughEscapeAndPressesElsewhere)
{
    TransientSurfaces open;
    const auto drawer = open.Open({ .Owner = "level", .Dismiss = TransientDismiss::Light });
    open.SetPinned(drawer, true);
    EXPECT_FALSE(open.Escape());
    open.PressedOutside({});
    EXPECT_TRUE(open.IsOpen(drawer));
    open.SetPinned(drawer, false);
    EXPECT_TRUE(open.Escape());
}

TEST(TransientSurfaces, ClosingOneClosesWhatWasOpenedFromIt)
{
    TransientSurfaces open;
    std::vector<TransientSurfaces::Id> closed;
    open.SetClosedObserver([&](TransientSurfaces::Id id) { closed.push_back(id); });
    const auto drawer = open.Open({ .Owner = "level", .Dismiss = TransientDismiss::Explicit });
    const auto picker = open.Open({ .Owner = "level", .Parent = drawer, .Dismiss = TransientDismiss::Light });
    open.Close(drawer);
    EXPECT_FALSE(open.IsOpen(picker));
    EXPECT_EQ(closed, (std::vector<TransientSurfaces::Id>{ picker, drawer })) << "a child is heard of before its parent";
}

TEST(TransientSurfaces, APressInsideAChildSparesItsAncestors)
{
    TransientSurfaces open;
    const auto explorer = open.Open({ .Owner = "level", .Dismiss = TransientDismiss::Light });
    const auto filter = open.Open({ .Owner = "level", .Parent = explorer, .Dismiss = TransientDismiss::Light });
    const auto other = open.Open({ .Owner = "shell", .Dismiss = TransientDismiss::Light });
    open.PressedOutside({ filter });
    EXPECT_TRUE(open.IsOpen(filter));
    EXPECT_TRUE(open.IsOpen(explorer));
    EXPECT_FALSE(open.IsOpen(other));
}

TEST(TransientSurfaces, AnOwnerBoundOneGoesOnlyWithItsOwner)
{
    TransientSurfaces open;
    const auto card = open.Open({ .Owner = "level", .Dismiss = TransientDismiss::OwnerBound });
    EXPECT_FALSE(open.Escape());
    open.PressedOutside({});
    EXPECT_TRUE(open.IsOpen(card));
    open.CloseOwnedBy("level");
    EXPECT_FALSE(open.IsOpen(card));
}
