#include <platform/WindowFrameHit.h>

#include <gtest/gtest.h>

namespace
{
WindowFrameRegions Regions(int32_t border, bool enabled)
{
    WindowFrameRegions regions;
    regions.Caption[0] = WindowRect{ 0, 0, 120, 30 };    // identity plate
    regions.Caption[1] = WindowRect{ 300, 0, 500, 30 };  // free strip
    regions.CaptionCount = 2;
    regions.CaptionEnabled = enabled;
    regions.ResizeBorder = border;
    return regions;
}

WindowFrameProbe Probe(int32_t x, int32_t y, bool resizable = true, bool maximized = false)
{
    return WindowFrameProbe{ x, y, WindowExtent{ 1280, 720 }, resizable, maximized };
}
}

TEST(WindowFrameHit, InteriorAndMenusAreClient)
{
    const WindowFrameRegions regions = Regions(6, true);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(640, 360)), WindowFrameHit::Client);
    // Between the plate and the strip sit the menus: not published, so Client.
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(200, 15)), WindowFrameHit::Client);
}

TEST(WindowFrameHit, CaptionOnlyWhileEnabled)
{
    EXPECT_EQ(ClassifyWindowFrameHit(Regions(6, true), Probe(60, 15)), WindowFrameHit::Caption);
    EXPECT_EQ(ClassifyWindowFrameHit(Regions(6, true), Probe(400, 15)), WindowFrameHit::Caption);
    // Same geometry, dragging not permitted (a menu is open): Client.
    EXPECT_EQ(ClassifyWindowFrameHit(Regions(6, false), Probe(60, 15)), WindowFrameHit::Client);
    // The rect's far edges are exclusive.
    EXPECT_EQ(ClassifyWindowFrameHit(Regions(6, true), Probe(120, 15)), WindowFrameHit::Client);
    EXPECT_EQ(ClassifyWindowFrameHit(Regions(6, true), Probe(60, 30)), WindowFrameHit::Client);
}

TEST(WindowFrameHit, EdgesAndCornersInsideTheBorder)
{
    const WindowFrameRegions regions = Regions(6, true);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(640, 5)), WindowFrameHit::ResizeTop);
    // Just inside the border, in the menu gap: not an edge, not a caption rect.
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(200, 6)), WindowFrameHit::Client);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(640, 714)), WindowFrameHit::ResizeBottom);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(640, 713)), WindowFrameHit::Client);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(5, 360)), WindowFrameHit::ResizeLeft);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(1274, 360)), WindowFrameHit::ResizeRight);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(2, 2)), WindowFrameHit::ResizeTopLeft);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(1277, 2)), WindowFrameHit::ResizeTopRight);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(2, 717)), WindowFrameHit::ResizeBottomLeft);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(1277, 717)), WindowFrameHit::ResizeBottomRight);
}

TEST(WindowFrameHit, EdgeWinsOverCaption)
{
    // The plate touches the top edge; its outer pixels resize rather than drag.
    EXPECT_EQ(ClassifyWindowFrameHit(Regions(6, true), Probe(60, 3)), WindowFrameHit::ResizeTop);
    EXPECT_EQ(ClassifyWindowFrameHit(Regions(6, true), Probe(60, 6)), WindowFrameHit::Caption);
}

TEST(WindowFrameHit, StateDisablesEdgesButNotTheCaption)
{
    const WindowFrameRegions regions = Regions(6, true);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(200, 3, true, true)), WindowFrameHit::Client);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(60, 3, true, true)), WindowFrameHit::Caption);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(200, 3, false, false)), WindowFrameHit::Client);
    EXPECT_EQ(ClassifyWindowFrameHit(Regions(0, true), Probe(200, 3)), WindowFrameHit::Client);
    // With edges off, the strip's outer pixels drag instead.
    EXPECT_EQ(ClassifyWindowFrameHit(Regions(0, true), Probe(640, 3)), WindowFrameHit::Caption);
}

TEST(WindowFrameHit, DegenerateInputsAreClient)
{
    WindowFrameRegions none;
    none.CaptionEnabled = true;
    none.ResizeBorder = 6;
    EXPECT_EQ(ClassifyWindowFrameHit(none, Probe(60, 15)), WindowFrameHit::Client);

    const WindowFrameRegions regions = Regions(6, true);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, WindowFrameProbe{ 60, 15, WindowExtent{ 0, 0 }, true, false }),
              WindowFrameHit::Client);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(-1, 15)), WindowFrameHit::Client);
    EXPECT_EQ(ClassifyWindowFrameHit(regions, Probe(1280, 15)), WindowFrameHit::Client);

    // A count past the storage is clamped, never read out of bounds.
    WindowFrameRegions over = regions;
    over.CaptionCount = 9;
    EXPECT_EQ(ClassifyWindowFrameHit(over, Probe(400, 15)), WindowFrameHit::Caption);
}
