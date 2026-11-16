#include <sencha/testing/UiLayoutTest.h>
#include <sencha/testing/UiPointerEvents.h>

#include <cstdint>
#include <vector>

namespace
{
class UiStackOrderTest : public UiLayoutTest
{
protected:
    void SetUp() override
    {
        Content().Write("ui/pad.rml", R"(<rml><head><link type="text/rcss" href="pad.rcss"/></head>)"
                                      R"(<body data-model="pad"><div id="pad" data-event-click="pad_hit"/></body></rml>)");
        Content().Write("ui/pad.rcss", "body { display: block; width: 100%; height: 100%; }"
                                       "#pad { display: block; width: 100%; height: 100%; background-color: #ff0000; }");
        ASSERT_TRUE(Mount());
    }

    struct Pad
    {
        UiSurfaceId Surface;
        UiScreenHandle Screen;
    };

    Pad Open(std::uint32_t width, UiStackOrder order = kUiStackOrderDefault)
    {
        UiScreenDesc desc;
        desc.PackagePath = "asset://ui/pad.rml";
        desc.ModelName = "pad";
        desc.Actions = { "pad_hit" };
        const UiSurfaceId surface = Ui().CreateSurface("pad", RenderExtent{ width, 100 }, order);
        return { surface, Ui().OpenScreen(surface, desc) };
    }

    [[nodiscard]] std::vector<std::uint32_t> DrawnWidths()
    {
        Refresh();
        std::vector<std::uint32_t> widths;
        for (const UiDrawFrame& frame : Ui().Frames())
            widths.push_back(frame.Surface.Width);
        return widths;
    }

    [[nodiscard]] UiScreenHandle ClickedScreen()
    {
        UiClickAt(Ui(), 10.0f, 10.0f);
        Ui().Update();
        const std::vector<UiAction> actions = Ui().DrainActions();
        return actions.size() == 1 ? actions[0].Screen : UiScreenHandle{};
    }
};

using Widths = std::vector<std::uint32_t>;
} // namespace

TEST_F(UiStackOrderTest, AHigherOrderDrawsLaterAndHearsInputFirst)
{
    const Pad high = Open(100, UiStackOrder{ 5 });
    (void)Open(200, UiStackOrder{ -3 });
    (void)Open(300);
    EXPECT_EQ(DrawnWidths(), (Widths{ 200, 300, 100 }));
    EXPECT_EQ(ClickedScreen(), high.Screen);
    EXPECT_EQ(Ui().GetSurfaceStackOrder(high.Surface), UiStackOrder{ 5 });
    EXPECT_TRUE(NoErrors());
}

TEST_F(UiStackOrderTest, EqualOrdersStackInCreationOrderUntilOneIsRaised)
{
    const Pad first = Open(100);
    const Pad second = Open(200);
    EXPECT_EQ(DrawnWidths(), (Widths{ 100, 200 }));
    EXPECT_EQ(ClickedScreen(), second.Screen);

    Ui().SetSurfaceStackOrder(first.Surface, UiStackOrder{ 1 });
    EXPECT_EQ(DrawnWidths(), (Widths{ 200, 100 }));
    EXPECT_EQ(ClickedScreen(), first.Screen);
}

TEST_F(UiStackOrderTest, AReopenedSurfaceInAnEarlierSlotStillStacksByCreation)
{
    const Pad closing = Open(100);
    (void)Open(200);
    Ui().DestroySurface(closing.Surface);
    const Pad reopened = Open(300);
    ASSERT_EQ(reopened.Surface.Index, closing.Surface.Index) << "the new surface did not reuse the freed slot";
    EXPECT_EQ(DrawnWidths(), (Widths{ 200, 300 }));
    EXPECT_EQ(ClickedScreen(), reopened.Screen);
}

TEST_F(UiStackOrderTest, AReusedSlotKeepsNothingFromTheSurfaceBeforeIt)
{
    const Pad closing = Open(100, UiStackOrder{ 7 });
    Ui().SetSurfaceTexture(closing.Surface, "prop_screen");
    Ui().SetSurfacePlacement(closing.Surface, Rect2d(0.0f, 0.0f, 50.0f, 50.0f));
    Ui().DestroySurface(closing.Surface);
    const Pad reopened = Open(300);
    ASSERT_EQ(reopened.Surface.Index, closing.Surface.Index);
    EXPECT_EQ(Ui().GetSurfaceDestination(reopened.Surface), UiSurfaceDestination::Window);
    EXPECT_EQ(Ui().GetSurfaceInputPolicy(reopened.Surface), UiSurfaceInputPolicy::Full);
    EXPECT_FALSE(Ui().GetSurfacePlacement(reopened.Surface).has_value());
    EXPECT_EQ(Ui().GetSurfaceStackOrder(reopened.Surface), kUiStackOrderDefault);
}

TEST_F(UiStackOrderTest, AnOffscreenSurfaceNeverHearsInputBeforeAWindowSurface)
{
    const Pad window = Open(100);
    const Pad preview = Open(200, UiStackOrder{ 100 });
    Ui().SetSurfaceDestination(preview.Surface, UiSurfaceDestination::Offscreen);
    Ui().SetSurfacePlacement(preview.Surface, Rect2d(0.0f, 0.0f, 200.0f, 100.0f));
    EXPECT_EQ(DrawnWidths(), (Widths{ 100 }));
    EXPECT_EQ(ClickedScreen(), window.Screen);
}

TEST_F(UiStackOrderTest, TheShellStaysAboveTheHighestGameSurfaceMadeAfterIt)
{
    const Pad shell = Open(100, kUiStackOrderShell);
    (void)Open(200, UiStackOrder{ kUiStackOrderShell.Value - 1 });
    EXPECT_EQ(DrawnWidths(), (Widths{ 200, 100 }));
    EXPECT_EQ(ClickedScreen(), shell.Screen);
}
