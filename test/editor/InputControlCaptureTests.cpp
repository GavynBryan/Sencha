#include <gtest/gtest.h>

#include "input/InputControlCapture.h"

#include <input/InputControl.h>

#include <SDL3/SDL.h>

#include <array>
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

// A slot on a single-control binding driving a plane action: sticks and the
// mouse fit, buttons do not.
InputSlotAcceptance PlaneSlot()
{
    return InputSlotAcceptance{ InputBindingKind::Direct, InputActionType::Axis2D, true };
}

// One corner of a WASD-style composite: buttons only, whatever the action.
InputSlotAcceptance ButtonSlot()
{
    return InputSlotAcceptance{ InputBindingKind::Cardinal, InputActionType::Axis2D, true };
}

// A binding whose action the set does not declare yet: the slot takes whatever
// its shape allows.
InputSlotAcceptance UndecidedSlot()
{
    return InputSlotAcceptance{ InputBindingKind::Direct, InputActionType::Digital, false };
}

InputControlCapture Armed(std::string_view field,
                          InputSlotAcceptance acceptance = UndecidedSlot())
{
    InputControlCapture capture;
    capture.Arm(std::string(field), acceptance, std::string(kDocument), 7);
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
    capture.Arm("$.data.other", UndecidedSlot(), std::string(kDocument), 7);
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
    InputControlCapture capture = Armed("$.data.slot", ButtonSlot());

    // A pad resting against a thumb should not end a listen that is waiting for
    // a button press.
    EXPECT_FALSE(capture.HandleSdlEvent(StickPush(0.9f)));
    EXPECT_TRUE(capture.IsArmed());

    ASSERT_TRUE(capture.HandleSdlEvent(KeyPress(SDL_SCANCODE_J)));
    EXPECT_EQ(*capture.TakeCapture("$.data.slot", kDocument, 7), "key.j");
}

TEST(InputControlCapture, APlaneSlotAcceptsAStick)
{
    InputControlCapture capture = Armed("$.data.slot", PlaneSlot());

    EXPECT_TRUE(capture.HandleSdlEvent(StickPush(0.9f)));
    EXPECT_EQ(*capture.TakeCapture("$.data.slot", kDocument, 7), "gamepad.left_stick");
}

TEST(InputControlCapture, ListenRefusesWhatThePickerWouldNotOffer)
{
    // The failure this pairing exists to prevent: a control the binder rejects
    // must not be reachable by pressing it either.
    const InputSlotAcceptance buttonAction{
        InputBindingKind::Direct, InputActionType::Digital, true };
    InputControlCapture capture = Armed("$.data.slot", buttonAction);

    SDL_Event trigger{};
    trigger.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    trigger.gaxis.axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    trigger.gaxis.value = static_cast<Sint16>(0.9f * 32767.0f);

    EXPECT_FALSE(capture.HandleSdlEvent(trigger));
    EXPECT_TRUE(capture.IsArmed()) << "the listen waits for a control that fits";
}

TEST(InputControlCapture, RestingDriftDoesNotBind)
{
    InputControlCapture capture = Armed("$.data.slot", PlaneSlot());

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

// ---------------------------------------------------------------------------
// The offer set and the accept set must not drift
// ---------------------------------------------------------------------------

TEST(InputSlotAcceptanceRule, OffersExactlyWhatTheBinderWouldAccept)
{
    // The picker offers what Accepts() allows and the binder takes what
    // BindingProducesType allows. This walks every control this platform can
    // name against every shape and action type and asserts the two agree, so a
    // future pairing cannot become bindable in one and not the other.
    constexpr std::array kKinds{ InputBindingKind::Direct,
                                 InputBindingKind::AxisPair,
                                 InputBindingKind::Cardinal };
    constexpr std::array kTypes{ InputActionType::Digital,
                                 InputActionType::Axis1D,
                                 InputActionType::Axis2D };

    for (const InputBindingKind kind : kKinds)
    {
        for (const InputActionType type : kTypes)
        {
            const InputSlotAcceptance acceptance{ kind, type, true };
            for (const NamedInputControl& entry : EnumerateInputControls())
            {
                InputBinding probe;
                probe.Kind = kind;
                probe.Controls[kBindingNegativeX] = entry.Control;

                const bool offered = acceptance.Accepts(entry.Control.Source);
                const bool bindable = BindingKindAcceptsControl(kind, entry.Control.Source)
                    && BindingProducesType(probe, type);
                EXPECT_EQ(offered, bindable)
                    << entry.Name << " for a " << static_cast<int>(kind)
                    << " binding driving type " << static_cast<int>(type);
            }
        }
    }
}
