#include "shell/TransientSurfaces.h"

#include <gtest/gtest.h>

#include <vector>

// Escape takes away one thing at a time, the closest first, and says when there
// is nothing left so the workspace's own steps can run. Whoever opened one
// hears when it closes, however it closed.

TEST(TransientSurfaces, EscapeTakesTheHighestPriorityThenTheNewest)
{
    TransientSurfaces open;
    const auto drawer = open.Open(1);
    const auto popover = open.Open(2);
    const auto tray = open.Open(1);

    EXPECT_TRUE(open.Escape());
    EXPECT_FALSE(open.IsOpen(popover));
    EXPECT_TRUE(open.Escape());
    EXPECT_FALSE(open.IsOpen(tray)) << "among equals, the newer one goes first";
    EXPECT_TRUE(open.IsOpen(drawer));
    EXPECT_TRUE(open.Escape());
    EXPECT_FALSE(open.Escape()) << "nothing left: the workspace's escape steps run next";
    EXPECT_FALSE(open.AnyOpen());
}

TEST(TransientSurfaces, WhoeverOpenedOneHearsItCloseOnceHoweverItClosed)
{
    TransientSurfaces open;
    std::vector<TransientSurfaces::Id> closed;
    open.SetClosedObserver([&](TransientSurfaces::Id id) { closed.push_back(id); });
    const auto escaped = open.Open();
    const auto shut = open.Open();

    open.Close(shut);
    open.Close(shut);
    EXPECT_TRUE(open.Escape());
    EXPECT_EQ(closed, (std::vector<TransientSurfaces::Id>{ shut, escaped }));
}
