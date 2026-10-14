#include <gtest/gtest.h>
#include <input/GamepadDeviceSet.h>
#include <input/InputControl.h>
#include <input/InputFrame.h>
#include <input/SdlInputCapture.h>

#include <SDL3/SDL.h>

#include <algorithm>

// The capture layer's contract: held device state is only ever cleared by a real
// release or by ReleaseAllHeld().
//
// ReleaseAllHeld() is now for the one case where the device genuinely stops
// reporting -- focus loss, where the key-up is never sent -- and Accept() calls
// it there itself. A surface consuming input is NOT such a case: PlatformEventRouter
// folds every event in before any consumer is offered it, so a claimed release
// still arrives. See PlatformEventRouterTests.cpp.

namespace
{
std::size_t CountLogged(const InputFrame& frame, InputControlSource source, uint32_t index, bool down)
{
    const auto log = frame.TransitionLog();
    return static_cast<std::size_t>(std::count_if(log.begin(), log.end(), [&](const InputTransition& t) {
        return t.Control == InputControl{ source, static_cast<std::uint16_t>(index) } && t.Down == down;
    }));
}

bool Logged(const InputFrame& frame, InputControlSource source, uint32_t index, bool down)
{
    return CountLogged(frame, source, index, down) > 0;
}

SDL_Event KeyEvent(uint32_t type, SDL_Scancode scancode)
{
    SDL_Event event{};
    event.type = type;
    event.key.scancode = scancode;
    event.key.repeat = false;
    return event;
}

SDL_Event MouseButtonEvent(uint32_t type, uint8_t button)
{
    SDL_Event event{};
    event.type = type;
    event.button.button = button;
    return event;
}
}

TEST(InputFrameRelease, HeldKeysBecomeReleaseEdges)
{
    InputFrame frame;
    frame.SetKeyHeld(SDL_SCANCODE_W, true);
    frame.SetKeyHeld(SDL_SCANCODE_SPACE, true);
    frame.SetMouseButtonHeld(SDL_BUTTON_LEFT, true);

    frame.ReleaseAllHeld();

    EXPECT_FALSE(frame.IsKeyDown(SDL_SCANCODE_W));
    EXPECT_FALSE(frame.IsKeyDown(SDL_SCANCODE_SPACE));
    EXPECT_FALSE(frame.IsMouseButtonDown(SDL_BUTTON_LEFT));
    EXPECT_TRUE(Logged(frame, InputControlSource::Key, SDL_SCANCODE_W, false));
    EXPECT_TRUE(Logged(frame, InputControlSource::Key, SDL_SCANCODE_SPACE, false));
    EXPECT_TRUE(Logged(frame, InputControlSource::MouseButton, SDL_BUTTON_LEFT, false));
    for (const InputTransition& transition : frame.TransitionLog())
        EXPECT_TRUE(transition.Synthetic) << "the device sent none of these";
}

// Transitions keep the order the platform reported them in, across devices, and
// a press-release-press of one key in one frame is three transitions, not one.
TEST(InputFrameLog, KeepsEveryTransitionInArrivalOrder)
{
    InputFrame frame;
    SdlInputCapture::Accept(frame, KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE));
    SdlInputCapture::Accept(frame, MouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT));
    SdlInputCapture::Accept(frame, KeyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_SPACE));
    SdlInputCapture::Accept(frame, KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE));

    const auto log = frame.TransitionLog();
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[0].Control.Source, InputControlSource::Key);
    EXPECT_TRUE(log[0].Down);
    EXPECT_EQ(log[1].Control.Source, InputControlSource::MouseButton);
    EXPECT_FALSE(log[2].Down);
    EXPECT_TRUE(log[3].Down);
    for (const InputTransition& transition : log)
        EXPECT_FALSE(transition.Synthetic);
}

TEST(InputFrameLog, OverflowDropsTransitionsButKeepsHeldStateExact)
{
    InputFrame frame;
    const InputControl space{ InputControlSource::Key, SDL_SCANCODE_SPACE };
    for (std::size_t i = 0; i < kInputFrameTransitionCapacity + 5; ++i)
        frame.SetButton(space, i % 2 == 0);
    EXPECT_EQ(frame.TransitionLog().size(), kInputFrameTransitionCapacity);
    EXPECT_EQ(frame.DroppedTransitions, 5u);
    EXPECT_TRUE(frame.IsKeyDown(SDL_SCANCODE_SPACE)) << "the last of an odd count of transitions is a press";
}

TEST(InputFrameRelease, IsIdempotent)
{
    InputFrame frame;
    frame.SetKeyHeld(SDL_SCANCODE_W, true);

    frame.ReleaseAllHeld();
    ASSERT_EQ(CountLogged(frame, InputControlSource::Key, SDL_SCANCODE_W, false), 1u);

    // Callers hold the condition, not the transition: releasing again while
    // nothing is held has to stay silent or a UI layer that releases every
    // frame would emit a release edge every frame.
    frame.ReleaseAllHeld();
    EXPECT_EQ(CountLogged(frame, InputControlSource::Key, SDL_SCANCODE_W, false), 1u);
}

TEST(InputFrameRelease, DropsPendingMotion)
{
    InputFrame frame;
    frame.MouseDeltaX = 40.0f;
    frame.MouseDeltaY = -12.0f;
    frame.MouseWheelY = 3.0f;

    frame.ReleaseAllHeld();

    EXPECT_FLOAT_EQ(frame.MouseDeltaX, 0.0f);
    EXPECT_FLOAT_EQ(frame.MouseDeltaY, 0.0f);
    EXPECT_FLOAT_EQ(frame.MouseWheelY, 0.0f);
}

TEST(InputFrameRelease, PressAndReleaseInOneFrameKeepsBothEdges)
{
    InputFrame frame;
    SdlInputCapture::Accept(frame, KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE));
    ASSERT_TRUE(Logged(frame, InputControlSource::Key, SDL_SCANCODE_SPACE, true));

    frame.ReleaseAllHeld();

    // A tap that starts and ends inside one frame is still a tap: dropping the
    // press edge here would lose the impulse entirely.
    EXPECT_TRUE(Logged(frame, InputControlSource::Key, SDL_SCANCODE_SPACE, true));
    EXPECT_TRUE(Logged(frame, InputControlSource::Key, SDL_SCANCODE_SPACE, false));
}

TEST(SdlInputCapture, FocusLossReleasesHeldInput)
{
    InputFrame frame;
    SdlInputCapture::Accept(frame, KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W));
    SdlInputCapture::Accept(frame, MouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT));
    ASSERT_TRUE(frame.IsKeyDown(SDL_SCANCODE_W));

    SDL_Event focusLost{};
    focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    SdlInputCapture::Accept(frame, focusLost);

    // An unfocused window never delivers the key-up, so without this the player
    // walks forever after alt-tabbing mid-stride.
    EXPECT_FALSE(frame.IsKeyDown(SDL_SCANCODE_W));
    EXPECT_FALSE(frame.IsMouseButtonDown(SDL_BUTTON_RIGHT));
    EXPECT_TRUE(Logged(frame, InputControlSource::Key, SDL_SCANCODE_W, false));
}

TEST(SdlInputCapture, BeginFrameKeepsHeldStateAndEdges)
{
    InputFrame frame;
    SdlInputCapture::Accept(frame, KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W));
    frame.MouseDeltaX = 5.0f;

    SdlInputCapture::BeginFrame(frame);

    // Deltas are per-frame accumulations; held state and unconsumed edges are
    // not, because a frame that runs no tick still owes them to the next one.
    EXPECT_TRUE(frame.IsKeyDown(SDL_SCANCODE_W));
    EXPECT_TRUE(Logged(frame, InputControlSource::Key, SDL_SCANCODE_W, true));
    EXPECT_FLOAT_EQ(frame.MouseDeltaX, 0.0f);
}

// ---------------------------------------------------------------------------
// Folding several pads into one abstract pad
// ---------------------------------------------------------------------------

namespace
{
constexpr std::uint32_t kPadA = 1;
constexpr std::uint32_t kPadB = 2;
constexpr std::uint32_t kSouth = static_cast<std::uint32_t>(GamepadButton::South);

// Both pads open and nothing pressed, which is where every scenario below
// starts and where the frame starts too.
GamepadDeviceSet TwoPads()
{
    GamepadDeviceSet devices;
    devices.Add(kPadA);
    devices.Add(kPadB);
    return devices;
}

// One device change, published the way the capture adapter publishes it.
void PressOn(GamepadDeviceSet& devices, InputFrame& frame, std::uint32_t device, bool down)
{
    const std::uint32_t before = devices.FoldButtons();
    devices.SetButton(device, kSouth, down);
    devices.PublishButtons(frame, before);
}
}

TEST(GamepadDeviceSet, AButtonStaysDownWhileAnyPadHoldsIt)
{
    GamepadDeviceSet devices = TwoPads();
    InputFrame frame;

    PressOn(devices, frame, kPadA, true);
    PressOn(devices, frame, kPadB, true);
    ASSERT_TRUE(frame.IsGamepadButtonDown(kSouth));
    EXPECT_EQ(CountLogged(frame, InputControlSource::GamepadButton, kSouth, true), 1u) << "one abstract pad, one press";

    // The first pad lets go while the second is still holding it.
    frame.ClearTransitions();
    PressOn(devices, frame, kPadA, false);
    EXPECT_TRUE(frame.IsGamepadButtonDown(kSouth));
    EXPECT_TRUE(!Logged(frame, InputControlSource::GamepadButton, kSouth, false));

    // Only when the last one lets go does the abstract pad come up.
    PressOn(devices, frame, kPadB, false);
    EXPECT_FALSE(frame.IsGamepadButtonDown(kSouth));
    EXPECT_TRUE(Logged(frame, InputControlSource::GamepadButton, kSouth, false));
}

TEST(GamepadDeviceSet, UnpluggingOnePadLeavesWhatTheOthersHold)
{
    GamepadDeviceSet devices = TwoPads();
    InputFrame frame;
    PressOn(devices, frame, kPadA, true);
    PressOn(devices, frame, kPadB, true);
    frame.ClearTransitions();

    // A pad pulled out mid-press never sends its button-up.
    const std::uint32_t before = devices.FoldButtons();
    devices.Remove(kPadA);
    devices.PublishButtons(frame, before);

    EXPECT_TRUE(frame.IsGamepadButtonDown(kSouth)) << "the pad still plugged in is still holding it";
    EXPECT_TRUE(!Logged(frame, InputControlSource::GamepadButton, kSouth, false));

    const std::uint32_t beforeLast = devices.FoldButtons();
    devices.Remove(kPadB);
    devices.PublishButtons(frame, beforeLast);
    EXPECT_FALSE(frame.IsGamepadButtonDown(kSouth));
    EXPECT_TRUE(Logged(frame, InputControlSource::GamepadButton, kSouth, false));
}

TEST(GamepadDeviceSet, AStickTakesTheDevicePushedFurthest)
{
    GamepadDeviceSet devices = TwoPads();
    InputFrame frame;

    devices.SetAxis(kPadA, GamepadAxis::LeftX, 0.9f);
    devices.SetAxis(kPadB, GamepadAxis::LeftY, -0.2f);
    devices.PublishAxes(frame);
    EXPECT_FLOAT_EQ(frame.GetGamepadAxis(GamepadAxis::LeftX), 0.9f);
    EXPECT_FLOAT_EQ(frame.GetGamepadAxis(GamepadAxis::LeftY), 0.0f)
        << "both axes come from the same hand";

    // The stick that was winning returns to centre, and the fold follows it
    // there rather than holding the furthest value it ever saw.
    devices.SetAxis(kPadA, GamepadAxis::LeftX, 0.0f);
    devices.PublishAxes(frame);
    EXPECT_FLOAT_EQ(frame.GetGamepadAxis(GamepadAxis::LeftY), -0.2f);

    // And a pad unplugged with its stick pushed stops contributing at all.
    devices.Remove(kPadB);
    devices.PublishAxes(frame);
    EXPECT_FLOAT_EQ(frame.GetGamepadAxis(GamepadAxis::LeftY), 0.0f);
}

TEST(GamepadDeviceSet, PublishingDoesNotFightAForcedRelease)
{
    GamepadDeviceSet devices = TwoPads();
    InputFrame frame;
    PressOn(devices, frame, kPadA, true);

    // Focus loss releases everything the frame is holding while the device
    // itself keeps reporting the button down.
    frame.ReleaseAllHeld();
    frame.ClearTransitions();

    // An unrelated device change must not resurrect it as a fresh press.
    devices.SetAxis(kPadB, GamepadAxis::RightX, 0.5f);
    devices.PublishAxes(frame);
    devices.PublishButtons(frame, devices.FoldButtons());
    EXPECT_FALSE(frame.IsGamepadButtonDown(kSouth));
    EXPECT_TRUE(!Logged(frame, InputControlSource::GamepadButton, kSouth, true));
}
