#include <sencha/testing/UiLayoutTest.h>

#include <ui/UiStandaloneScreen.h>

#include <cstddef>

namespace
{
constexpr UiWindowView kDesk{ RenderExtent{ 1280, 720 }, 1.0f };
constexpr UiWindowView kHiDpi{ RenderExtent{ 2560, 1440 }, 2.0f };

class UiWindowSurfaceTest : public UiLayoutTest
{
protected:
    void SetUp() override
    {
        Content().Write("ui/hud.rml", R"(<rml><head><link type="text/rcss" href="/ui/base.rcss"/></head>)"
                                      R"(<body><div id="panel"/></body></rml>)");
        ASSERT_TRUE(Mount());
    }

    [[nodiscard]] UiScreenDesc Hud() const
    {
        UiScreenDesc desc;
        desc.PackagePath = "asset://ui/hud.rml";
        return desc;
    }

    [[nodiscard]] std::size_t Refusals()
    {
        std::size_t count = 0;
        for (const UiDiagnostic& diagnostic : Ui().DrainDiagnostics())
            count += diagnostic.Severity == UiDiagnosticSeverity::Error ? 1 : 0;
        return count;
    }

    void ExpectView(UiSurfaceId surface, const UiWindowView& view)
    {
        EXPECT_EQ(Ui().GetSurfaceSize(surface).Width, view.Extent.Width);
        EXPECT_EQ(Ui().GetSurfaceSize(surface).Height, view.Extent.Height);
        EXPECT_FLOAT_EQ(Ui().GetSurfaceScale(surface), view.Scale);
    }
};
} // namespace

TEST_F(UiWindowSurfaceTest, ASurfaceCannotFollowAWindowThatHasPublishedNoView)
{
    EXPECT_FALSE(Ui().GetWindowView().has_value());
    EXPECT_FALSE(Ui().CreateWindowSurface("hud").IsValid());
    (void)Ui().DrainDiagnostics();
}

TEST_F(UiWindowSurfaceTest, AFollowingSurfaceTakesEveryViewItsWindowPublishes)
{
    Ui().SetWindowView(0, kDesk);
    const UiSurfaceId following = Ui().CreateWindowSurface("hud");
    ASSERT_TRUE(following.IsValid());
    EXPECT_TRUE(Ui().FollowsWindow(following));
    ExpectView(following, kDesk);

    const UiSurfaceId fixed = Ui().CreateSurface("fixed", RenderExtent{ 300, 200 });
    EXPECT_FALSE(Ui().FollowsWindow(fixed));

    Ui().SetWindowView(0, kHiDpi);
    ExpectView(following, kHiDpi);
    EXPECT_EQ(Ui().GetSurfaceSize(fixed).Width, 300u) << "a surface that does not follow was resized";
    EXPECT_FLOAT_EQ(Ui().GetSurfaceScale(fixed), 1.0f);

    Ui().SetSurfaceSize(following, RenderExtent{ 640, 480 });
    Ui().SetWindowView(0, kHiDpi);
    ExpectView(following, kHiDpi);
}

TEST_F(UiWindowSurfaceTest, AMinimisedWindowLeavesItsSurfacesAtTheLastRealView)
{
    Ui().SetWindowView(0, kHiDpi);
    const UiSurfaceId following = Ui().CreateWindowSurface("hud");
    Ui().SetWindowView(0, UiWindowView{ RenderExtent{ 0, 0 }, 1.0f });
    ExpectView(following, kHiDpi);
    ASSERT_TRUE(Ui().GetWindowView().has_value());
    EXPECT_EQ(*Ui().GetWindowView(), kHiDpi);
}

TEST_F(UiWindowSurfaceTest, AFollowingSurfaceMovedToAnotherWindowTakesThatWindowsView)
{
    constexpr std::uint32_t kSecondWindow = 42;
    Ui().SetWindowView(0, kDesk);
    Ui().SetWindowView(kSecondWindow, kHiDpi);
    const UiSurfaceId following = Ui().CreateWindowSurface("hud");
    ExpectView(following, kDesk);

    Ui().SetSurfaceWindow(following, kSecondWindow);
    ExpectView(following, kHiDpi);
    Ui().SetWindowView(0, UiWindowView{ RenderExtent{ 800, 600 }, 1.0f });
    ExpectView(following, kHiDpi);
}

TEST_F(UiWindowSurfaceTest, AStandaloneScreenWaitsForTheServiceAndTheWindow)
{
    UiStandaloneScreen screen;
    EXPECT_FALSE(screen.OpenOverWindow(nullptr, "hud", Hud(), UiSurfaceInputPolicy::Disabled));
    EXPECT_FALSE(screen.OpenOverWindow(&Ui(), "hud", Hud(), UiSurfaceInputPolicy::Disabled))
        << "opened over a window that has no size yet";

    Ui().SetWindowView(0, kDesk);
    ASSERT_TRUE(screen.OpenOverWindow(&Ui(), "hud", Hud(), UiSurfaceInputPolicy::Disabled));
    EXPECT_EQ(screen.Ui(), &Ui());
    EXPECT_TRUE(Ui().IsScreenOpen(screen.Screen()));
    EXPECT_TRUE(Ui().FollowsWindow(screen.Surface()));
    EXPECT_EQ(Ui().GetSurfaceInputPolicy(screen.Surface()), UiSurfaceInputPolicy::Disabled);

    Ui().SetWindowView(0, kHiDpi);
    ExpectView(screen.Surface(), kHiDpi);

    const UiScreenHandle first = screen.Screen();
    EXPECT_TRUE(screen.OpenOverWindow(&Ui(), "hud", Hud(), UiSurfaceInputPolicy::Disabled));
    EXPECT_EQ(screen.Screen(), first);
    EXPECT_TRUE(NoErrors());
}

TEST_F(UiWindowSurfaceTest, AStandaloneScreenOpensAtTheStackOrderItIsGiven)
{
    Ui().SetWindowView(0, kDesk);
    UiStandaloneScreen screen;
    ASSERT_TRUE(screen.OpenOverWindow(&Ui(), "hud", Hud(), UiSurfaceInputPolicy::Disabled, UiStackOrder{ 12 }));
    EXPECT_EQ(Ui().GetSurfaceStackOrder(screen.Surface()), UiStackOrder{ 12 });
    screen.Close();
}

TEST_F(UiWindowSurfaceTest, ClosingAStandaloneScreenDestroysItsSurface)
{
    Ui().SetWindowView(0, kDesk);
    UiStandaloneScreen screen;
    ASSERT_TRUE(screen.OpenOverWindow(&Ui(), "hud", Hud(), UiSurfaceInputPolicy::Full));
    const UiSurfaceId surface = screen.Surface();
    const UiScreenHandle opened = screen.Screen();

    screen.Close();
    EXPECT_FALSE(screen.IsOpen());
    EXPECT_FALSE(Ui().IsScreenOpen(opened));
    EXPECT_EQ(Ui().GetSurfaceSize(surface).Width, 0u) << "the surface outlived its screen";

    EXPECT_TRUE(screen.OpenOverWindow(&Ui(), "hud", Hud(), UiSurfaceInputPolicy::Full)) << "a closed screen reopens";
    screen.Close();
}

TEST_F(UiWindowSurfaceTest, AScreenThatFailedToOpenIsNotRetriedUntilClosed)
{
    Ui().SetWindowView(0, kDesk);
    UiScreenDesc missing;
    missing.PackagePath = "asset://ui/missing.rml";
    UiStandaloneScreen screen;

    EXPECT_FALSE(screen.OpenOverWindow(&Ui(), "hud", missing, UiSurfaceInputPolicy::Disabled));
    EXPECT_GT(Refusals(), 0u);
    EXPECT_FALSE(screen.Surface().IsValid()) << "a failed open left its surface behind";

    EXPECT_FALSE(screen.OpenOverWindow(&Ui(), "hud", missing, UiSurfaceInputPolicy::Disabled));
    EXPECT_EQ(Refusals(), 0u) << "a failed open was tried again every frame";

    screen.Close();
    EXPECT_FALSE(screen.OpenOverWindow(&Ui(), "hud", missing, UiSurfaceInputPolicy::Disabled));
    EXPECT_GT(Refusals(), 0u) << "Close did not let it try again";
}

TEST_F(UiWindowSurfaceTest, AStandaloneScreenInATextureKeepsItsOwnExtent)
{
    UiStandaloneScreen screen;
    ASSERT_TRUE(screen.OpenInTexture(&Ui(), "prop", Hud(), RenderExtent{ 256, 128 }, "prop_screen"));
    EXPECT_EQ(Ui().GetSurfaceDestination(screen.Surface()), UiSurfaceDestination::Texture);
    EXPECT_EQ(Ui().GetSurfaceTexture(screen.Surface()), "prop_screen");
    EXPECT_EQ(Ui().GetSurfaceInputPolicy(screen.Surface()), UiSurfaceInputPolicy::Disabled);
    EXPECT_FALSE(Ui().FollowsWindow(screen.Surface()));

    Ui().SetWindowView(0, kHiDpi);
    EXPECT_EQ(Ui().GetSurfaceSize(screen.Surface()).Width, 256u);
    EXPECT_EQ(Ui().GetSurfaceSize(screen.Surface()).Height, 128u);
    EXPECT_FLOAT_EQ(Ui().GetSurfaceScale(screen.Surface()), 1.0f);
    screen.Close();
    EXPECT_TRUE(NoErrors());
}
