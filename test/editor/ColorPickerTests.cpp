#include <gtest/gtest.h>

#include "shell/ColorPicker.h"
#include "properties/ColorValue.h"
#include "properties/PropertySource.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/logging/LoggingProvider.h>
#include <project/ProjectContentMount.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

// A colour property picked on a hue and saturation wheel with a value bar,
// through the property's own edit session: a drag previews, letting go is one
// step, and closing in the middle puts back what was there.

#ifndef SENCHA_EDITOR_UI_DIR
#define SENCHA_EDITOR_UI_DIR "."
#endif

namespace
{
bool Near(double a, double b, double tolerance = 1e-3)
{
    return std::fabs(a - b) <= tolerance;
}

// One colour property that records how it was edited.
class ColourSource final : public IPropertySource
{
public:
    [[nodiscard]] std::vector<PropertyNode> Nodes() const override { return {}; }
    bool BeginEdit(std::string_view key) override
    {
        Began = std::string(key);
        return true;
    }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view value) override
    {
        Previewed.emplace_back(value);
        return PropertyEditResult::Applied;
    }
    PropertyEditResult CommitEdit(std::string_view, std::string_view value) override
    {
        Committed.emplace_back(value);
        return PropertyEditResult::Applied;
    }
    void CancelEdit(std::string_view) override { ++Cancelled; }

    std::string Began;
    std::vector<std::string> Previewed;
    std::vector<std::string> Committed;
    int Cancelled = 0;
};

class Harness
{
public:
    Harness()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        MountEditorContent(SENCHA_EDITOR_UI_DIR, Assets, Logging, nullptr);
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages, Assets.Fonts, nullptr, nullptr);
        Surface = Ui->CreateSurface("test", RenderExtent{ 1280, 720 });
        Picker = std::make_unique<ColorPicker>(*Ui, Surface);
    }
    ~Harness()
    {
        Picker.reset();
        Ui->Shutdown();
    }

    void Open(std::string_view value)
    {
        Picker->Open({ .Source = [this] { return &Source; }, .Key = "color", .Label = "Color" }, value,
                     UiElementBox{ .X = 900.0f, .Y = 100.0f, .Width = 300.0f, .Height = 24.0f });
        Ui->Update();
    }

    void Frame()
    {
        (void)Picker->Update();
        Ui->Update();
    }

    void Pointer(std::uint32_t type, float x, float y)
    {
        SDL_Event event{};
        event.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION)
        {
            event.motion.x = x;
            event.motion.y = y;
        }
        else
        {
            event.button.button = SDL_BUTTON_LEFT;
            event.button.x = x;
            event.button.y = y;
        }
        (void)Ui->ProcessPlatformEvent(event);
    }

    [[nodiscard]] UiElementBox Wheel() const
    {
        return Ui->MeasureElement(Picker->CurrentScreen(), "color_wheel", UiBoxArea::Content).value_or(UiElementBox{});
    }

    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    std::unique_ptr<ColorPicker> Picker;
    ColourSource Source;
};
} // namespace

TEST(ColorValue, ReadsAndWritesAColourKeepingWhatFollowsItsChannels)
{
    const std::optional<ColorValue> rgba = ParseColorValue("1, 0.5, 0.25, 0.8");
    ASSERT_TRUE(rgba.has_value());
    EXPECT_EQ(rgba->Color.G, 0.5);
    ASSERT_EQ(rgba->Rest.size(), 1u);
    EXPECT_EQ(ColorValueText(*rgba), "1, 0.5, 0.25, 0.8") << "alpha was lost on the way back";
    EXPECT_FALSE(ParseColorValue("1, 0.5").has_value());
    EXPECT_FALSE(ParseColorValue("red").has_value());
    EXPECT_EQ(ColorSwatch({ 2.0, 0.5, -1.0 }), "rgba(255, 128, 0, 255)");
}

TEST(ColorValue, HueSaturationAndValueRoundTrip)
{
    struct Case
    {
        Rgb Color;
        Hsv Expected;
    };
    const Case cases[] = {
        { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 1.0 } },
        { { 0.0, 1.0, 0.0 }, { 1.0 / 3.0, 1.0, 1.0 } },
        { { 0.0, 0.0, 0.5 }, { 2.0 / 3.0, 1.0, 0.5 } },
        { { 0.5, 0.5, 0.5 }, { 0.0, 0.0, 0.5 } },
        { { 1.0, 0.5, 0.25 }, { 1.0 / 18.0, 0.75, 1.0 } },
    };
    for (const Case& c : cases)
    {
        const Hsv hsv = HsvOf(c.Color);
        EXPECT_TRUE(Near(hsv.H, c.Expected.H) && Near(hsv.S, c.Expected.S) && Near(hsv.V, c.Expected.V))
            << c.Color.R << ", " << c.Color.G << ", " << c.Color.B;
        const Rgb back = RgbOf(hsv);
        EXPECT_TRUE(Near(back.R, c.Color.R) && Near(back.G, c.Color.G) && Near(back.B, c.Color.B));
    }
}

TEST(ColorWheel, RedIsAtTheTopAndHueTurnsClockwise)
{
    const Vec2d red = ColorWheelPoint({ 0.0, 1.0, 1.0 });
    EXPECT_TRUE(Near(red.X, 0.0) && Near(red.Y, -1.0));
    const Vec2d green = ColorWheelPoint({ 1.0 / 3.0, 1.0, 1.0 });
    EXPECT_GT(green.X, 0.0f) << "a third of a turn clockwise from the top is to the right";

    const Hsv picked = ColorWheelPick(ColorWheelPoint({ 0.6, 0.4, 1.0 }), 0.7);
    EXPECT_TRUE(Near(picked.H, 0.6) && Near(picked.S, 0.4) && Near(picked.V, 0.7));
    EXPECT_TRUE(Near(ColorWheelPick({ 0.0f, 3.0f }, 1.0).S, 1.0)) << "past the rim is the rim";
}

TEST(ColorPicker, ADragAcrossTheWheelPreviewsAndLettingGoIsOneStep)
{
    Harness harness;
    harness.Open("1, 1, 1");
    ASSERT_TRUE(harness.Picker->IsOpen());
    const UiElementBox wheel = harness.Wheel();
    ASSERT_GT(wheel.Width, 0.0f) << "the wheel was not laid out";
    const float cx = wheel.X + wheel.Width * 0.5f;
    const float cy = wheel.Y + wheel.Height * 0.5f;

    // Pressed at the middle, dragged to the top of the rim: full red.
    harness.Pointer(SDL_EVENT_MOUSE_MOTION, cx, cy);
    harness.Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, cx, cy);
    harness.Frame();
    harness.Pointer(SDL_EVENT_MOUSE_MOTION, cx, cy - wheel.Height * 0.25f);
    harness.Pointer(SDL_EVENT_MOUSE_MOTION, cx, wheel.Y + 1.0f);
    harness.Frame();
    EXPECT_EQ(harness.Source.Began, "color");
    ASSERT_FALSE(harness.Source.Previewed.empty()) << "the drag showed nothing";
    EXPECT_TRUE(harness.Source.Committed.empty()) << "a drag in progress was already a step";

    harness.Pointer(SDL_EVENT_MOUSE_BUTTON_UP, cx, wheel.Y + 1.0f);
    harness.Frame();
    ASSERT_EQ(harness.Source.Committed.size(), 1u);
    const std::optional<ColorValue> red = ParseColorValue(harness.Source.Committed.front());
    ASSERT_TRUE(red.has_value());
    EXPECT_TRUE(Near(red->Color.R, 1.0, 0.02) && red->Color.G < 0.1 && red->Color.B < 0.1)
        << harness.Source.Committed.front();
    EXPECT_EQ(harness.Source.Cancelled, 0);
}

TEST(ColorPicker, ClosingMidDragPutsBackWhatWasThere)
{
    Harness harness;
    harness.Open("0.2, 0.4, 0.6");
    const UiElementBox wheel = harness.Wheel();
    const float cx = wheel.X + wheel.Width * 0.5f;
    const float cy = wheel.Y + wheel.Height * 0.5f;
    harness.Pointer(SDL_EVENT_MOUSE_MOTION, cx, cy);
    harness.Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, cx, cy);
    harness.Frame();
    ASSERT_FALSE(harness.Source.Previewed.empty());

    harness.Picker->Close();
    EXPECT_EQ(harness.Source.Cancelled, 1) << "the preview was left standing";
    EXPECT_TRUE(harness.Source.Committed.empty());
}

TEST(ColorPicker, APressOutsideItClosesIt)
{
    Harness harness;
    harness.Open("1, 1, 1");
    harness.Pointer(SDL_EVENT_MOUSE_MOTION, 20.0f, 700.0f);
    harness.Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, 20.0f, 700.0f);
    harness.Pointer(SDL_EVENT_MOUSE_BUTTON_UP, 20.0f, 700.0f);
    EXPECT_FALSE(harness.Picker->Update()) << "a press elsewhere left it open";
    EXPECT_FALSE(harness.Picker->IsOpen());
}
