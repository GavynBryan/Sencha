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

// Two UI layers, and the order they combine in.
//
// Kyusu hosts an ImGui shell and an authored document layer at once. The shell
// carries a hole where the viewport is, because input over the 3D region belongs
// to the scene even though an ImGui window is technically there. An authored
// document has no such hole, so folding it in before the hole rather than after
// it would punch a viewport-shaped gap through a modal dialog.

TEST(UiCaptureComposition, TheViewportHoleOnlyAppliesToTheShell)
{
    const UiInputCapture shell{ .Mouse = true, .Keyboard = false };
    const UiInputCapture none{};

    EXPECT_FALSE(CombineUiCapture(shell, true, none).Mouse)
        << "the shell kept the mouse over the viewport region";
    EXPECT_TRUE(CombineUiCapture(shell, false, none).Mouse);
}

TEST(UiCaptureComposition, AnAuthoredScrimOverTheViewportStillSwallowsTheClick)
{
    // The dialog case. The scrim covers the viewport on purpose; a click on it
    // must not also pick an entity behind it.
    const UiInputCapture shell{ .Mouse = true, .Keyboard = false };
    const UiInputCapture authored{ .Mouse = true, .Keyboard = false };

    EXPECT_TRUE(CombineUiCapture(shell, true, authored).Mouse)
        << "a click on a modal scrim fell through to the tools it was blocking";
}

TEST(UiCaptureComposition, AFocusedAuthoredFieldOwnsItsLetters)
{
    // Typing "4" into an inspector row must not also fire whatever shortcut the
    // editor binds to 4.
    const UiInputCapture shell{};
    const UiInputCapture authored{ .Mouse = false, .Keyboard = true };

    EXPECT_TRUE(CombineUiCapture(shell, false, authored).Keyboard);
    EXPECT_TRUE(CombineUiCapture(shell, true, authored).Keyboard)
        << "the viewport hole took the keyboard away from a focused text field";
    EXPECT_FALSE(CombineUiCapture(shell, false, authored).Mouse)
        << "a focused text field claimed the mouse as well";
}
