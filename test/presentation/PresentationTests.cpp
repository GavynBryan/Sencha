// Rendering to several windows through one renderer, against a real device.
// Each presentation gets an ordinary MainColor feature of its own that clears
// it to one colour; per-presentation capture proves each window shows its own
// feature and nobody else's. Skips without a display, like the golden images.

#include <assets/texture/ImageLoader.h>
#include <core/config/EngineConfig.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/PresentationFramePlan.h>
#include <graphics/vulkan/PresentationTarget.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <platform/WindowCreateInfo.h>
#include <render/RenderLight.h>
#include <render/extract/Camera.h>
#include <render/feature/SkyRenderFeature.h>

#include <DeviceTestSupport.h>
#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Rgb = std::array<std::uint8_t, 3>;

    // Clears its presentation's colour attachment and does nothing else.
    class SolidColorFeature final : public IRenderFeature
    {
    public:
        explicit SolidColorFeature(Rgb color) : Color(color) {}
        [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
        [[nodiscard]] bool Setup(const RenderFeatureServices&) override { return true; }
        void OnDraw(const RenderFrame& frame) override
        {
            const FrameContext& context = *frame.Backend;
            VkClearAttachment clear{};
            clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clear.colorAttachment = 0;
            clear.clearValue.color = { { Color[0] / 255.0f, Color[1] / 255.0f, Color[2] / 255.0f, 1.0f } };
            VkClearRect rect{};
            rect.rect.extent = context.TargetExtent;
            rect.layerCount = 1;
            vkCmdClearAttachments(context.Cmd, 1, &clear, 1, &rect);
            ++Draws;
        }
        int Draws = 0;

    private:
        Rgb Color;
    };

    constexpr Rgb kRed{ 255, 0, 0 };
    constexpr Rgb kGreen{ 0, 255, 0 };
    constexpr Rgb kBlue{ 0, 0, 255 };
    constexpr Rgb kWhite{ 255, 255, 255 };

    class PresentationTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            if (!DisplayAvailable())
                GTEST_SKIP() << "no display; presentations need a window system";
            setenv("SENCHA_PRESENT_MODE", "IMMEDIATE", 1);
            Logging.AddSink<ValidationErrorCount>(ValidationErrors);
            Config.Graphics.EnableValidation = true;
            Config.Window.Width = 320;
            Config.Window.Height = 240;
            Platform = std::make_unique<PlatformServices>(Logging);
            SdlWindow* primary = Platform->CreatePrimaryWindow(Config.Window);
            ASSERT_NE(primary, nullptr);
            Graphics = std::make_unique<GraphicsServices>(Logging, Config, *primary, Platform->Windows);
            ASSERT_TRUE(Graphics->IsValid());
            Output = std::filesystem::temp_directory_path() / "sencha_presentation_tests";
            std::filesystem::create_directories(Output);
        }

        void TearDown() override
        {
            Graphics.reset();
            Platform.reset();
            EXPECT_EQ(ValidationErrors.load(), 0);
        }

        Renderer& Render() { return Graphics->MainRenderer; }
        VulkanFrameService& Frames() { return Graphics->Frames; }

        struct Window
        {
            PresentationId Presentation;
            std::uint32_t WindowId = 0;
        };

        Window Open(const char* title, PresentationDesc desc = {})
        {
            WindowCreateInfo info;
            info.Title = title;
            info.Width = 240;
            info.Height = 180;
            info.GraphicsApi = WindowGraphicsApi::Vulkan;
            SdlWindow* window = Platform->Windows.CreateWindow(info);
            if (window == nullptr)
                return {};
            return { Render().CreatePresentation(*window, desc), window->GetId() };
        }

        SolidColorFeature* Fill(PresentationId presentation, Rgb color)
        {
            return Render().AddFeature(std::make_unique<SolidColorFeature>(color),
                                       FeatureRegistration{ .Scope = RenderFeatureScope::For(presentation) });
        }

        // Pumps the window system the way the engine's platform phase does,
        // then renders one frame, rebuilding what the windows asked for.
        RenderFrameResult Frame()
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
                Platform->Windows.HandleEvent(event);
            PresentationTarget& primary = *Frames().FindPresentation(Frames().PrimaryPresentation());
            if (primary.ExtentChanged() && primary.Availability() == PresentationAvailability::Acquirable)
                Frames().RebuildPresentation(Frames().PrimaryPresentation(), primary.Window().GetExtent());
            Frames().RebuildStaleSecondaries();
            const RenderFrameResult result = Render().DrawFrameScheduled();
            Frames().ForEachPresentation([](PresentationId, PresentationTarget& target) {
                EXPECT_EQ(target.ImageLedger().Held(), 0u) << "a frame presents every image it acquires";
            });
            return result;
        }

        void Frames(int count)
        {
            for (int i = 0; i < count; ++i)
                Frame();
        }

        // The colour at the centre of `presentation`'s next finished frame.
        std::optional<Rgb> Capture(PresentationId presentation, const std::string& name)
        {
            const std::filesystem::path path = Output / (name + ".png");
            std::filesystem::remove(path);
            if (!Render().CaptureFrame(presentation, path.string(), Render().GetFramesDrawn() + 1))
                return std::nullopt;
            for (int i = 0; i < 30 && !std::filesystem::exists(path); ++i)
                Frame();
            const std::optional<Image> image = LoadImageFromFile(path.string());
            if (!image.has_value())
                return std::nullopt;
            const std::size_t pixel = (static_cast<std::size_t>(image->Height / 2) * image->Width + image->Width / 2) * 4;
            return Rgb{ image->Pixels[pixel], image->Pixels[pixel + 1], image->Pixels[pixel + 2] };
        }

        // Takes every image `presentation`'s swapchain will hand out, so the
        // frame's own acquire for it returns none.
        int HoldEveryImage(PresentationId presentation)
        {
            const VkDevice device = Graphics->Device.GetDevice();
            const VkSwapchainKHR chain = Frames().FindPresentation(presentation)->Swapchain().GetSwapchain();
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence acquired = VK_NULL_HANDLE;
            if (vkCreateFence(device, &fenceInfo, nullptr, &acquired) != VK_SUCCESS)
                return 0;
            int held = 0;
            constexpr std::uint64_t kTimeoutNs = 100'000'000;
            std::uint32_t index = 0;
            for (VkResult result = vkAcquireNextImageKHR(device, chain, kTimeoutNs, VK_NULL_HANDLE, acquired, &index);
                 result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
                 result = vkAcquireNextImageKHR(device, chain, kTimeoutNs, VK_NULL_HANDLE, acquired, &index))
            {
                vkWaitForFences(device, 1, &acquired, VK_TRUE, kTimeoutNs);
                vkResetFences(device, 1, &acquired);
                ++held;
            }
            vkDestroyFence(device, acquired, nullptr);
            return held;
        }

        bool DestroyAndWait(Window window)
        {
            bool windowGone = false;
            const bool accepted = Render().DestroyPresentation(window.Presentation, [&, id = window.WindowId] {
                windowGone = Platform->Windows.DestroyWindow(id);
            });
            for (int i = 0; accepted && i < 10 && !windowGone; ++i)
                Frame();
            return accepted && windowGone;
        }

        LoggingProvider Logging;
        std::atomic<int> ValidationErrors{ 0 };
        EngineConfig Config;
        std::unique_ptr<PlatformServices> Platform;
        std::unique_ptr<GraphicsServices> Graphics;
        std::filesystem::path Output;
    };
}

TEST_F(PresentationTest, ThreeWindowsEachShowTheirOwnFeature)
{
    const Window b = Open("B");
    const Window c = Open("C", PresentationDesc{ .DepthStencil = false });
    ASSERT_TRUE(b.Presentation.IsValid());
    ASSERT_TRUE(c.Presentation.IsValid());
    ASSERT_NE(Fill(Render().PrimaryPresentation(), kRed), nullptr);
    SolidColorFeature* green = Fill(b.Presentation, kGreen);
    SolidColorFeature* blue = Fill(c.Presentation, kBlue);
    ASSERT_NE(green, nullptr);
    ASSERT_NE(blue, nullptr);

    Frames(5);
    EXPECT_GT(green->Draws, 0);
    EXPECT_GT(blue->Draws, 0);
    EXPECT_EQ(Capture(Render().PrimaryPresentation(), "primary"), kRed);
    EXPECT_EQ(Capture(b.Presentation, "b"), kGreen);
    EXPECT_EQ(Capture(c.Presentation, "c"), kBlue) << "a window without depth still shows its feature";
}

TEST_F(PresentationTest, ResizingOneWindowLeavesTheOthersRendering)
{
    const Window b = Open("B");
    const Window c = Open("C");
    Fill(Render().PrimaryPresentation(), kRed);
    Fill(b.Presentation, kGreen);
    SolidColorFeature* blue = Fill(c.Presentation, kBlue);
    Frames(3);

    Platform->Windows.GetWindow(b.WindowId)->SetSize(300, 200);
    const int blueBefore = blue->Draws;
    for (int i = 0; i < 30 && Frames().FindPresentation(b.Presentation)->Swapchain().GetExtent().width != 300; ++i)
        Frame();
    EXPECT_EQ(Frames().FindPresentation(b.Presentation)->Swapchain().GetExtent().width, 300u);
    EXPECT_GT(blue->Draws, blueBefore);
    EXPECT_EQ(Capture(b.Presentation, "b_resized"), kGreen);
    EXPECT_EQ(Capture(c.Presentation, "c_after_resize"), kBlue);
}

TEST_F(PresentationTest, AWindowIsDestroyedOnlyOnceNothingRecordsIntoIt)
{
    const Window b = Open("B");
    const Window c = Open("C");
    Fill(Render().PrimaryPresentation(), kRed);
    SolidColorFeature* green = Fill(b.Presentation, kGreen);
    Fill(c.Presentation, kBlue);
    Frames(3);

    EXPECT_FALSE(Render().DestroyPresentation(b.Presentation, {})) << "a bound presentation fails closed";
    EXPECT_NE(Frames().FindPresentation(b.Presentation), nullptr);
    EXPECT_FALSE(Render().DestroyPresentation(Render().PrimaryPresentation(), {}));

    ASSERT_TRUE(Render().RemoveFeature(green));
    ASSERT_TRUE(DestroyAndWait(b));
    EXPECT_EQ(Frames().FindPresentation(b.Presentation), nullptr);
    EXPECT_EQ(Capture(c.Presentation, "c_after_destroy"), kBlue);

    const Window d = Open("D");
    ASSERT_TRUE(d.Presentation.IsValid());
    EXPECT_EQ(Frames().FindPresentation(b.Presentation), nullptr) << "B's id never names D";
    EXPECT_FALSE(Render().CaptureFrame(b.Presentation, (Output / "stale.png").string(), 0));
    EXPECT_EQ(Render().AddFeature(std::make_unique<SolidColorFeature>(kWhite),
                                  FeatureRegistration{ .Scope = RenderFeatureScope::For(b.Presentation) }),
              nullptr);
    Fill(d.Presentation, kWhite);
    EXPECT_EQ(Capture(d.Presentation, "d"), kWhite);
    EXPECT_EQ(Capture(Render().PrimaryPresentation(), "primary_after_d"), kRed);
}

TEST_F(PresentationTest, WindowsGoInAnyOrder)
{
    for (const bool firstCreatedFirst : { true, false })
    {
        const Window b = Open("B");
        const Window c = Open("C");
        SolidColorFeature* green = Fill(b.Presentation, kGreen);
        SolidColorFeature* blue = Fill(c.Presentation, kBlue);
        Frames(3);
        ASSERT_TRUE(Render().RemoveFeature(green));
        ASSERT_TRUE(Render().RemoveFeature(blue));
        const std::array<Window, 2> order = firstCreatedFirst ? std::array{ b, c } : std::array{ c, b };
        for (const Window& window : order)
            EXPECT_TRUE(DestroyAndWait(window));
        EXPECT_EQ(Frames().PresentationCount(), 1u);
    }
    // And the renderer tears down with secondaries still open.
    const Window e = Open("E");
    Fill(e.Presentation, kGreen);
    Frames(2);
}

// Two stalled windows cost a frame at most the acquire deadline together while
// the others render. A timed-out acquire signals nothing, so each slot reuses its
// semaphore without a validation error, and rebuilt windows render again.
TEST_F(PresentationTest, StalledWindowsTogetherCostAFrameAtMostTheAcquireDeadline)
{
    const float refreshHz = Frames().FindPresentation(Render().PrimaryPresentation())->Window().GetDisplayRefreshHz();
    if (refreshHz <= 0.0f)
        GTEST_SKIP() << "the display reports no refresh rate, so a frame's acquires have no deadline";
    const Window b = Open("B");
    const Window c = Open("C");
    const Window d = Open("D");
    SolidColorFeature* red = Fill(Render().PrimaryPresentation(), kRed);
    SolidColorFeature* green = Fill(b.Presentation, kGreen);
    SolidColorFeature* blue = Fill(c.Presentation, kBlue);
    SolidColorFeature* white = Fill(d.Presentation, kWhite);
    Frames(3);
    ASSERT_GT(HoldEveryImage(b.Presentation), 0);
    ASSERT_GT(HoldEveryImage(c.Presentation), 0);

    const double deadlineSeconds = kAcquireDeadlineRefreshes / static_cast<double>(refreshHz);
    const double timerSlackSeconds = 0.5 / static_cast<double>(refreshHz);
    const int greenBefore = green->Draws;
    const int blueBefore = blue->Draws;
    for (std::uint32_t frame = 0; frame < 2 * Frames().GetFramesInFlight(); ++frame)
    {
        const int redBefore = red->Draws;
        const int whiteBefore = white->Draws;
        EXPECT_EQ(Frame(), RenderFrameResult::Presented);
        EXPECT_GT(red->Draws, redBefore);
        EXPECT_GT(white->Draws, whiteBefore) << "D acquires after both stalled windows";
        EXPECT_LE(Frames().GetLastTiming().AcquireWaitSeconds, deadlineSeconds + timerSlackSeconds);
    }
    EXPECT_EQ(green->Draws, greenBefore);
    EXPECT_EQ(blue->Draws, blueBefore);

    for (const Window& stalled : { b, c })
        ASSERT_TRUE(Frames().RebuildPresentation(stalled.Presentation,
                                                 Platform->Windows.GetWindow(stalled.WindowId)->GetExtent()));
    Frames(3);
    EXPECT_GT(green->Draws, greenBefore);
    EXPECT_GT(blue->Draws, blueBefore);
}

TEST_F(PresentationTest, ALoadTimePipelineMatchesTheDepthStencilScopeItDrawsInto)
{
    // Built at Setup, before any frame: the stencil aspect the swapchain scope
    // binds has to be in the formats it was compiled against.
    CameraRenderData camera;
    RenderLightSet lights;
    lights.AmbientSky = Vec<3>(0.0f, 1.0f, 0.0f);
    lights.AmbientGround = Vec<3>(0.0f, 1.0f, 0.0f);
    lights.TonemapEnabled = false;
    ASSERT_NE(Render().AddFeature(std::make_unique<SkyRenderFeature>(camera, lights)), nullptr);

    const std::optional<Rgb> centre = Capture(Render().PrimaryPresentation(), "sky");
    ASSERT_TRUE(centre.has_value());
    EXPECT_GT((*centre)[1], 200) << "the sky drew";
    EXPECT_LT((*centre)[0], 50);
}

namespace
{
    // Sets up or refuses as told, and records when the renderer tears it down.
    class LifecycleFeature final : public IRenderFeature
    {
    public:
        LifecycleFeature(std::string_view name, bool setsUp, std::vector<std::string_view>& tornDown)
            : Name(name), SetsUp(setsUp), TornDown(tornDown) {}
        [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
        [[nodiscard]] bool Setup(const RenderFeatureServices&) override { return SetsUp; }
        void OnDraw(const RenderFrame&) override {}
        void Teardown() override { TornDown.push_back(Name); }

    private:
        std::string_view Name;
        bool SetsUp;
        std::vector<std::string_view>& TornDown;
    };

    constexpr std::array<std::string_view, 1> kOnProvider{ "test_provider" };
}

TEST_F(PresentationTest, ABatchWithAFeatureThatFailsSetupRegistersNoneOfIt)
{
    std::vector<std::string_view> tornDown;
    Render().StageFeature(std::make_unique<LifecycleFeature>("provider", true, tornDown),
                          FeatureRegistration{ .Id = "test_provider" });
    Render().StageFeature(std::make_unique<LifecycleFeature>("independent", true, tornDown),
                          FeatureRegistration{ .Id = "test_independent" });
    Render().StageFeature(std::make_unique<LifecycleFeature>("refused", false, tornDown),
                          FeatureRegistration{ .Id = "test_refused", .DependsOn = kOnProvider });

    EXPECT_FALSE(Render().CommitStagedFeatures());
    EXPECT_EQ(tornDown, (std::vector<std::string_view>{ "refused", "independent", "provider" }))
        << "what set up before the refusal is withdrawn, last set up first";

    IRenderFeature* again = Render().AddFeature(std::make_unique<LifecycleFeature>("again", true, tornDown),
                                                FeatureRegistration{ .Id = "test_independent" });
    ASSERT_NE(again, nullptr) << "a withdrawn feature's id is free again";
    EXPECT_TRUE(Render().RemoveFeature(again));
}

TEST_F(PresentationTest, AddingAFeatureLeavesAnotherHostsStagedBatchAlone)
{
    std::vector<std::string_view> tornDown;
    Render().StageFeature(std::make_unique<LifecycleFeature>("staged", false, tornDown),
                          FeatureRegistration{ .Id = "test_staged" });

    IRenderFeature* added = Render().AddFeature(std::make_unique<LifecycleFeature>("added", true, tornDown),
                                                FeatureRegistration{ .Id = "test_added" });
    ASSERT_NE(added, nullptr) << "a feature added alone neither commits nor fails with a batch staged elsewhere";
    EXPECT_TRUE(tornDown.empty());

    EXPECT_FALSE(Render().CommitStagedFeatures());
    EXPECT_EQ(tornDown, (std::vector<std::string_view>{ "staged" }));
    EXPECT_TRUE(Render().RemoveFeature(added));
}

TEST_F(PresentationTest, AFeatureAddedByNameIsCheckedAgainstWhatIsRegistered)
{
    std::vector<std::string_view> tornDown;
    IRenderFeature* provider = Render().AddFeature(std::make_unique<LifecycleFeature>("provider", true, tornDown),
                                                   FeatureRegistration{ .Id = "test_provider" });
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(Render().AddFeature(std::make_unique<LifecycleFeature>("duplicate", true, tornDown),
                                  FeatureRegistration{ .Id = "test_provider" }),
              nullptr);
    constexpr std::array<std::string_view, 1> onMissing{ "test_missing" };
    EXPECT_EQ(Render().AddFeature(std::make_unique<LifecycleFeature>("orphan", true, tornDown),
                                  FeatureRegistration{ .Id = "test_orphan", .DependsOn = onMissing }),
              nullptr);

    IRenderFeature* consumer = Render().AddFeature(std::make_unique<LifecycleFeature>("consumer", true, tornDown),
                                                   FeatureRegistration{ .Id = "test_consumer", .DependsOn = kOnProvider });
    ASSERT_NE(consumer, nullptr);
    EXPECT_FALSE(Render().RemoveFeature(provider)) << "a consumer still borrows from it";
    EXPECT_TRUE(Render().RemoveFeature(consumer));
    EXPECT_TRUE(Render().RemoveFeature(provider));
}
