#include "input/InputRouter.h"
#include "input/UiInputGuard.h"

#include <SDL3/SDL_keycode.h>
#include <gtest/gtest.h>

// The guard owns a key's release exactly when it owned its press: a field
// that took the letter takes the release too, and nothing else does.
namespace
{
class UiInputGuardTest : public testing::Test
{
protected:
    void SetUp() override
    {
        Router.AddHandler(MakeUiInputGuard([this] { return Capture; }));
        Router.AddHandler([this](const InputEvent&, PointerCapture&)
        {
            ++Reached;
            return InputConsumed::No;
        });
    }

    InputRouter Router;
    UiInputCapture Capture{};
    int Reached = 0;
};
}

TEST_F(UiInputGuardTest, KeyReleaseFollowsTheKeyboardOwner)
{
    Capture.Keyboard = true;
    EXPECT_EQ(Router.Route(KeyDownEvent{ .Key = SDLK_Q, .Modifiers = {} }), InputConsumed::Yes);
    EXPECT_EQ(Router.Route(KeyUpEvent{ .Key = SDLK_Q, .Modifiers = {} }), InputConsumed::Yes);
    EXPECT_EQ(Reached, 0);

    Capture.Keyboard = false;
    EXPECT_EQ(Router.Route(KeyDownEvent{ .Key = SDLK_Q, .Modifiers = {} }), InputConsumed::No);
    EXPECT_EQ(Router.Route(KeyUpEvent{ .Key = SDLK_Q, .Modifiers = {} }), InputConsumed::No);
    EXPECT_EQ(Reached, 2);
}

TEST_F(UiInputGuardTest, KeyReleaseIsNotAPointerEvent)
{
    // The mouse being the UI's says nothing about a key.
    Capture.Mouse = true;
    EXPECT_EQ(Router.Route(KeyUpEvent{ .Key = SDLK_Q, .Modifiers = {} }), InputConsumed::No);
    EXPECT_EQ(Reached, 1);
}
