#include <gtest/gtest.h>
#include <input/InputFrame.h>
#include <input/SdlInputCapture.h>

#include <SDL3/SDL.h>

#include <algorithm>

// The capture layer's contract: held device state is only ever cleared by a
// real release or by ReleaseAllHeld(), so every path that stops events reaching
// the frame mid-press has to release explicitly or the key sticks down.

namespace
{
bool Contains(const std::vector<uint32_t>& values, uint32_t value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
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
    EXPECT_TRUE(Contains(frame.KeysReleased, SDL_SCANCODE_W));
    EXPECT_TRUE(Contains(frame.KeysReleased, SDL_SCANCODE_SPACE));
    EXPECT_TRUE(Contains(frame.MouseButtonsReleased, SDL_BUTTON_LEFT));
}

TEST(InputFrameRelease, IsIdempotent)
{
    InputFrame frame;
    frame.SetKeyHeld(SDL_SCANCODE_W, true);

    frame.ReleaseAllHeld();
    ASSERT_EQ(frame.KeysReleased.size(), 1u);

    // Callers hold the condition, not the transition: releasing again while
    // nothing is held has to stay silent or a UI layer that releases every
    // frame would emit a release edge every frame.
    frame.ReleaseAllHeld();
    EXPECT_EQ(frame.KeysReleased.size(), 1u);
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
    ASSERT_TRUE(Contains(frame.KeysPressed, SDL_SCANCODE_SPACE));

    frame.ReleaseAllHeld();

    // A tap that starts and ends inside one frame is still a tap: dropping the
    // press edge here would lose the impulse entirely.
    EXPECT_TRUE(Contains(frame.KeysPressed, SDL_SCANCODE_SPACE));
    EXPECT_TRUE(Contains(frame.KeysReleased, SDL_SCANCODE_SPACE));
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
    EXPECT_TRUE(Contains(frame.KeysReleased, SDL_SCANCODE_W));
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
    EXPECT_TRUE(Contains(frame.KeysPressed, SDL_SCANCODE_W));
    EXPECT_FLOAT_EQ(frame.MouseDeltaX, 0.0f);
}
