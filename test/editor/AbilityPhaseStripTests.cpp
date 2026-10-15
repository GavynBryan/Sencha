// The strip document over a real UI service: it opens and binds every row, a
// card's head or a row picks its element, and dragging an op onto another
// carries it there, the innermost target taking the drop.

#include "AbilityPhaseStrip.h"
#include "authoring/AbilityStrip.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <project/ProjectContentMount.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#ifndef SENCHA_EDITOR_UI_DIR
#define SENCHA_EDITOR_UI_DIR "."
#endif

namespace
{
constexpr std::string_view kAbility = R"({
    "phases": [
      { "key": "p_aim", "name": "Aim",
        "enter": [ { "key": "o_a", "op": "cue", "cue": "cue.a" }, { "key": "o_b", "op": "cue", "cue": "cue.b" } ],
        "then": [ { "key": "t_go", "on": "release", "to": "p_fire" } ] },
      { "key": "p_fire", "name": "Fire", "then": [ { "key": "t_end", "to": "end" } ] } ] })";

class Harness
{
public:
    Harness()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        MountEditorContent(SENCHA_EDITOR_UI_DIR, Assets, Logging, nullptr);
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages, Assets.Fonts, nullptr, nullptr);
        Surface = Ui->CreateSurface("test", RenderExtent{ 1280, 720 });
        Strip = std::make_unique<AbilityPhaseStrip>(*Ui);
        Strip->MoveTo(Surface);
        Strip->SetVisible(true);
        Rows = AbilityStripOf(*JsonParse(kAbility), {}, {});
        Frame();
        Frame();
    }
    ~Harness()
    {
        Strip.reset();
        Ui->Shutdown();
    }

    std::vector<AbilityStripRequest> Frame()
    {
        std::vector<AbilityStripRequest> asked = Strip->Update(Rect2d(Vec2d(0.0f, 0.0f), Vec2d(1280.0f, 720.0f)), Rows, "");
        Ui->Update();
        return asked;
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
        Ui->Update();
    }

    void Click(float x, float y)
    {
        Pointer(SDL_EVENT_MOUSE_MOTION, x, y);
        Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
        Pointer(SDL_EVENT_MOUSE_BUTTON_UP, x, y);
    }

    [[nodiscard]] UiElementBox Box(std::string_view id) const
    {
        return Ui->MeasureElement(Strip->CurrentScreen(), id, UiBoxArea::Border).value_or(UiElementBox{});
    }

    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    std::unique_ptr<AbilityPhaseStrip> Strip;
    AbilityStripRows Rows;
};

bool Asked(const std::vector<AbilityStripRequest>& asked, AbilityStripAsk ask, std::string_view key)
{
    return std::ranges::any_of(asked, [&](const AbilityStripRequest& request) {
        return request.Ask == ask && request.Key == key;
    });
}
} // namespace

TEST(AbilityPhaseStrip, ACardAndAnOpArePickedByTheirKeys)
{
    Harness strip;
    const UiElementBox fire = strip.Box("card:p_fire");
    ASSERT_GT(fire.Width, 0.0f) << "the second card was laid out";
    strip.Click(fire.X + 12.0f, fire.Y + 12.0f);
    EXPECT_TRUE(Asked(strip.Frame(), AbilityStripAsk::Pick, "p_fire"));

    const UiElementBox op = strip.Box("op:o_b");
    ASSERT_GT(op.Width, 0.0f) << "an op sits in its card's entry slot";
    EXPECT_GT(op.Y, strip.Box("op:o_a").Y) << "in order";
    strip.Click(op.X + op.Width * 0.5f, op.Y + op.Height * 0.5f);
    const std::vector<AbilityStripRequest> asked = strip.Frame();
    EXPECT_TRUE(Asked(asked, AbilityStripAsk::Pick, "o_b"));
    EXPECT_FALSE(Asked(asked, AbilityStripAsk::Pick, "p_aim")) << "a row's pick is not its card's";
}

TEST(AbilityPhaseStrip, AnOpDraggedOntoAnotherIsCarriedThere)
{
    Harness strip;
    const UiElementBox from = strip.Box("op:o_b");
    const UiElementBox onto = strip.Box("op:o_a");
    ASSERT_GT(from.Width, 0.0f);
    const float x = from.X + from.Width * 0.5f;
    strip.Pointer(SDL_EVENT_MOUSE_MOTION, x, from.Y + from.Height * 0.5f);
    strip.Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, x, from.Y + from.Height * 0.5f);
    for (float y = from.Y + from.Height * 0.5f; y >= onto.Y + onto.Height * 0.5f; y -= 2.0f)
        strip.Pointer(SDL_EVENT_MOUSE_MOTION, x, y);
    strip.Pointer(SDL_EVENT_MOUSE_BUTTON_UP, x, onto.Y + onto.Height * 0.5f);
    const std::vector<AbilityStripRequest> asked = strip.Frame();
    ASSERT_TRUE(Asked(asked, AbilityStripAsk::Carry, "o_b"));
    const auto drop = std::ranges::find(asked, AbilityStripAsk::Drop, &AbilityStripRequest::Ask);
    ASSERT_NE(drop, asked.end());
    EXPECT_EQ(drop->Key, "o_a") << "the innermost target is told first";
    EXPECT_TRUE(Asked(asked, AbilityStripAsk::Put, {}));
}
