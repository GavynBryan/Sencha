#include <gtest/gtest.h>

#include "input/InputControlCapture.h"

#include <SDL3/SDL.h>

#include <string>

// Press-to-bind claims raw events out from under the UI, so the rules about
// when it is listening and what it does with what it hears are the whole
// safety of the feature.

namespace
{
constexpr std::string_view kDocument = "asset://data/input_default.sdata";

SDL_Event KeyPress(SDL_Scancode scancode)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = scancode;
    event.key.repeat = false;
    return event;
}

SDL_Event StickPush(float deflection)
{
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    event.gaxis.value = static_cast<Sint16>(deflection * 32767.0f);
    return event;
}

InputControlCapture Armed(std::string_view field, bool buttonsOnly = false)
{
    InputControlCapture capture;
    capture.Arm(std::string(field), buttonsOnly, std::string(kDocument), 7);
    return capture;
}
}

TEST(InputControlCapture, IgnoresEventsUntilItIsArmed)
{
    InputControlCapture capture;

    // Every keystroke in the editor passes through here; consuming one while
    // nothing is listening would break typing everywhere.
    EXPECT_FALSE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_J)));
    EXPECT_FALSE(capture.IsArmed());
    EXPECT_FALSE(capture.TakeCapture("$.data.slot", kDocument, 7).has_value());
}

TEST(InputControlCapture, DeliversThePressedControlToTheSlotThatArmed)
{
    InputControlCapture capture = Armed("$.data.slot");
    ASSERT_TRUE(capture.IsArmedAt("$.data.slot"));

    EXPECT_TRUE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_J)));
    EXPECT_FALSE(capture.IsArmed()) << "one press ends the listen";

    const std::optional<std::string> captured =
        capture.TakeCapture("$.data.slot", kDocument, 7);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(*captured, "key.j");
}

TEST(InputControlCapture, DeliversOnlyOnce)
{
    InputControlCapture capture = Armed("$.data.slot");
    ASSERT_TRUE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_J)));

    ASSERT_TRUE(capture.TakeCapture("$.data.slot", kDocument, 7).has_value());
    // The form draws every frame; a capture that kept delivering would rewrite
    // the slot forever.
    EXPECT_FALSE(capture.TakeCapture("$.data.slot", kDocument, 7).has_value());
}

TEST(InputControlCapture, DeliversOnlyToTheSlotThatArmed)
{
    InputControlCapture capture = Armed("$.data.slot");
    ASSERT_TRUE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_J)));

    EXPECT_FALSE(capture.TakeCapture("$.data.other", kDocument, 7).has_value());
    EXPECT_TRUE(capture.TakeCapture("$.data.slot", kDocument, 7).has_value());
}

TEST(InputControlCapture, EscapeCancelsWithoutBinding)
{
    InputControlCapture capture = Armed("$.data.slot");

    EXPECT_TRUE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_ESCAPE)));
    EXPECT_FALSE(capture.IsArmed());
    EXPECT_FALSE(capture.TakeCapture("$.data.slot", kDocument, 7).has_value());
}

TEST(InputControlCapture, ArmingElsewhereAbandonsAnUncollectedCapture)
{
    InputControlCapture capture = Armed("$.data.slot");
    ASSERT_TRUE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_J)));

    // The author pressed a control, then clicked Listen on a different slot
    // before the first was drawn again. The abandoned capture must not land.
    capture.Arm("$.data.other", false, std::string(kDocument), 7);
    EXPECT_FALSE(capture.TakeCapture("$.data.slot", kDocument, 7).has_value());
    EXPECT_TRUE(capture.IsArmedAt("$.data.other"));
}

TEST(InputControlCapture, ADocumentEditBetweenArmingAndPressingDropsTheCapture)
{
    InputControlCapture capture = Armed("$.data.contexts[0].bindings[1].control");
    ASSERT_TRUE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_J)));

    // A binding was added or removed in between, so that path may name a
    // different binding now. Rebinding the wrong one silently is worse than
    // making the author press again.
    EXPECT_FALSE(capture
                     .TakeCapture("$.data.contexts[0].bindings[1].control", kDocument, 8)
                     .has_value());
}

TEST(InputControlCapture, ACaptureDoesNotCrossDocuments)
{
    InputControlCapture capture = Armed("$.data.slot");
    ASSERT_TRUE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_J)));

    EXPECT_FALSE(capture.TakeCapture("$.data.slot", "asset://data/other.sdata", 7).has_value());
}

TEST(InputControlCapture, AButtonSlotStaysArmedThroughStickMotion)
{
    InputControlCapture capture = Armed("$.data.slot", /*buttonsOnly=*/true);

    // A pad resting against a thumb should not end a listen that is waiting for
    // a button press.
    EXPECT_FALSE(capture.HandleSdlEvent(StickPush(0.9f)));
    EXPECT_TRUE(capture.IsArmed());

    ASSERT_TRUE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_J)));
    EXPECT_EQ(*capture.TakeCapture("$.data.slot", kDocument, 7), "key.j");
}

TEST(InputControlCapture, AnyControlSlotAcceptsAStick)
{
    InputControlCapture capture = Armed("$.data.slot");

    EXPECT_TRUE(capture.HandleSdlEvent(StickPush(0.9f)));
    EXPECT_EQ(*capture.TakeCapture("$.data.slot", kDocument, 7), "gamepad.left_stick");
}

TEST(InputControlCapture, RestingDriftDoesNotBind)
{
    InputControlCapture capture = Armed("$.data.slot");

    EXPECT_FALSE(capture.HandleSdlEvent(StickPush(0.1f)));
    EXPECT_TRUE(capture.IsArmed());
}

TEST(InputControlCapture, ListeningTwiceOnOneSlotIsAToggle)
{
    InputControlCapture capture = Armed("$.data.slot");
    capture.Disarm();

    EXPECT_FALSE(capture.IsArmed());
    EXPECT_FALSE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_J)));
}
