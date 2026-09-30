// Rendering to several windows through one renderer, against a real device.
// Each presentation gets an ordinary MainColor feature of its own that clears
// it to one colour; per-presentation capture proves each window shows its own
// feature and nobody else's. Skips without a display, like the golden images.

#include <assets/texture/ImageLoader.h>
#include <core/config/EngineConfig.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/PresentationTarget.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <platform/WindowCreateInfo.h>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
    bool DisplayAvailable()
    {
        const char* display = std::getenv("DISPLAY");
        const char* wayland = std::getenv("WAYLAND_DISPLAY");
        return (display != nullptr && display[0] != '\0') || (wayland != nullptr && wayland[0] != '\0');
    }

    class ValidationErrorCount : public ILogSink
    {
    public:
        explicit ValidationErrorCount(std::atomic<int>& count) : Count(count) {}
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            if (level >= LogLevel::Error && message.starts_with("[Vulkan]"))
            {
                ++Count;
                ADD_FAILURE() << message;
            }
        }

    private:
        std::atomic<int>& Count;
    };

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
                                       RenderFeatureScope::For(presentation));
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
            return Render().DrawFrameScheduled();
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
    EXPECT_EQ(Render().AddFeature(std::make_unique<SolidColorFeature>(kWhite), RenderFeatureScope::For(b.Presentation)),
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
