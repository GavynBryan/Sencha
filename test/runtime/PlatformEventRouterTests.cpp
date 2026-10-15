#include <gtest/gtest.h>
#include <input/InputFrame.h>
#include <input/PlatformEventRouter.h>
#include <input/SdlInputCapture.h>
#include <platform/SdlWindowService.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// The routing contract: the canonical device snapshot is folded before any
// consumer is offered the event, and a consumer claiming an event hides it from
// later consumers only -- never from the snapshot.
//
// The bug these protect against is not hypothetical. Routing consumers ahead of
// capture means a surface that claims a key-up throws away the release edge, and
// the key reads as held for as long as that surface is open. The engine used to
// compensate with a blanket ReleaseAllHeld() every frame the console was up,
// which cleared genuinely-held gameplay keys along with the stuck one.

namespace
{
SDL_Event KeyEvent(uint32_t type, SDL_Scancode scancode)
{
    SDL_Event event{};
    event.type = type;
    event.key.scancode = scancode;
    event.key.repeat = false;
    return event;
}

// A consumer that claims everything, standing in for an open console or a
// focused text field.
PlatformEventRouter::Consumer ClaimEverything()
{
    return [](const SDL_Event&) { return true; };
}
}

TEST(PlatformEventRouter, ClaimedEventStillReachesTheDeviceSnapshot)
{
    PlatformEventRouter router;
    router.AddConsumer("claims_everything", ClaimEverything());

    InputFrame frame;
    const bool claimed =
        router.Route(KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W), frame, nullptr);

    EXPECT_TRUE(claimed);
    EXPECT_TRUE(frame.IsKeyDown(SDL_SCANCODE_W))
        << "a claimed event must still be folded into the snapshot";
}

TEST(PlatformEventRouter, AKeyReleasedWhileASurfaceClaimsDoesNotStickDown)
{
    PlatformEventRouter router;
    bool surfaceOpen = false;
    router.AddConsumer("surface", [&surfaceOpen](const SDL_Event&) { return surfaceOpen; });

    InputFrame frame;
    // Pressed while nothing is claiming -- an ordinary gameplay press.
    router.Route(KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W), frame, nullptr);
    ASSERT_TRUE(frame.IsKeyDown(SDL_SCANCODE_W));

    // The console opens mid-press and claims the release.
    surfaceOpen = true;
    router.Route(KeyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_W), frame, nullptr);

    EXPECT_FALSE(frame.IsKeyDown(SDL_SCANCODE_W))
        << "the release edge must survive a surface claiming the event";
    const auto log = frame.TransitionLog();
    EXPECT_NE(std::find_if(log.begin(), log.end(),
                           [](const InputTransition& t) {
                               return t.Control == InputControl{ InputControlSource::Key, SDL_SCANCODE_W } && !t.Down
                                   && !t.Synthetic;
                           }),
              log.end())
        << "and must be reported as a release the player made, not merely cleared";
}

TEST(PlatformEventRouter, AClaimHidesTheEventFromLaterConsumersOnly)
{
    PlatformEventRouter router;
    std::vector<std::string> seen;

    router.AddConsumer("first", [&seen](const SDL_Event&) {
        seen.push_back("first");
        return true;
    });
    router.AddConsumer("second", [&seen](const SDL_Event&) {
        seen.push_back("second");
        return false;
    });

    InputFrame frame;
    EXPECT_TRUE(router.Route(KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_A), frame, nullptr));

    EXPECT_EQ(seen, std::vector<std::string>{ "first" });
    EXPECT_TRUE(frame.IsKeyDown(SDL_SCANCODE_A));
}

TEST(PlatformEventRouter, ConsumersAreOfferedInRegistrationOrder)
{
    PlatformEventRouter router;
    std::vector<std::string> seen;
    for (const char* name : { "highest", "middle", "lowest" })
    {
        router.AddConsumer(name, [&seen, name](const SDL_Event&) {
            seen.push_back(name);
            return false;
        });
    }

    InputFrame frame;
    EXPECT_FALSE(router.Route(KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_B), frame, nullptr))
        << "an unclaimed event must report so, or the caller skips its own handling";

    const std::vector<std::string> expected{ "highest", "middle", "lowest" };
    EXPECT_EQ(seen, expected);
}

TEST(PlatformEventRouter, NoConsumersStillCaptures)
{
    PlatformEventRouter router;
    InputFrame frame;

    EXPECT_FALSE(router.Route(KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_C), frame, nullptr));
    EXPECT_TRUE(frame.IsKeyDown(SDL_SCANCODE_C));
    EXPECT_EQ(router.ConsumerCount(), 0u);
}

TEST(PlatformEventRouter, SurfaceCaptureIsPublishedNotCompensatedFor)
{
    // The replacement for the blanket ReleaseAllHeld(): a raw reader learns the
    // keystroke was not aimed at it, and the snapshot still records what happened.
    InputFrame frame;
    frame.UiCapture.Keyboard = true;
    frame.SetKeyHeld(SDL_SCANCODE_ESCAPE, true);

    EXPECT_TRUE(frame.IsKeyDown(SDL_SCANCODE_ESCAPE));
    EXPECT_TRUE(frame.UiCapture.Keyboard);

    SdlInputCapture::BeginFrame(frame);
    EXPECT_FALSE(frame.UiCapture.Keyboard) << "a stale claim must not outlive its frame";
    EXPECT_TRUE(frame.IsKeyDown(SDL_SCANCODE_ESCAPE))
        << "clearing the claim must not disturb held device state";
}

TEST(PlatformEventRouter, AnEventIsRoutedByTheWindowItWasStampedWith)
{
    // Read from the event rather than resolved through a live window, so an
    // event arriving after its window closed goes nowhere instead of to the
    // primary, and routing is testable with no window at all.
    SDL_Event press{};
    press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    press.button.windowID = 7;
    EXPECT_EQ(SdlEventWindowId(press), 7u);

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.windowID = 9;
    EXPECT_EQ(SdlEventWindowId(key), 9u);

    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close.window.windowID = 3;
    EXPECT_EQ(SdlEventWindowId(close), 3u);

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    EXPECT_EQ(SdlEventWindowId(quit), 0u) << "an event naming no window names none";
}

// -- Pointer gestures ---------------------------------------------------------
//
// From a press to the release of the last button held, one owner hears a
// window's pointer: whoever claimed the press, or the application. A drag in
// the world carried over a menu, or a menu's drag carried over the world, is
// never split between them, and its release always reaches whoever began it.

namespace
{
SDL_Event Pointer(std::uint32_t type, std::uint32_t window = 1, std::uint8_t button = SDL_BUTTON_LEFT)
{
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION)
        event.motion.windowID = window;
    else
    {
        event.button.windowID = window;
        event.button.button = button;
    }
    return event;
}

SDL_Event WindowEvent(std::uint32_t type, std::uint32_t window = 1)
{
    SDL_Event event{};
    event.type = type;
    event.window.windowID = window;
    return event;
}

// A menu: it claims what lands on it, and here everything but presses does,
// as a release or a move over one of its buttons would.
struct Menu
{
    bool ClaimsPresses = false;
    int Heard = 0;
    std::vector<std::uint32_t> Ended;

    void AddTo(PlatformEventRouter& router)
    {
        router.AddConsumer(
            "menu",
            [this](const SDL_Event& event) {
                ++Heard;
                return event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || ClaimsPresses;
            },
            [this](std::uint32_t window) { Ended.push_back(window); });
    }
};
} // namespace

TEST(PlatformEventRouter, TheApplicationsDragIsNeverOfferedToAMenuItCrosses)
{
    PlatformEventRouter router;
    Menu menu;
    menu.AddTo(router);
    InputFrame frame;

    EXPECT_FALSE(router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN), frame, nullptr));
    ASSERT_TRUE(router.ApplicationHasGesture(1));
    menu.Heard = 0;

    EXPECT_FALSE(router.Route(Pointer(SDL_EVENT_MOUSE_MOTION), frame, nullptr)) << "the menu took the drag";
    EXPECT_FALSE(router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_UP), frame, nullptr))
        << "the menu took the release, and the drag in the world never ends";
    EXPECT_EQ(menu.Heard, 0);
    EXPECT_FALSE(router.HasGesture(1));

    EXPECT_TRUE(router.Route(Pointer(SDL_EVENT_MOUSE_MOTION), frame, nullptr)) << "with nothing held, hover is offered again";
}

TEST(PlatformEventRouter, AMenusDragNeverReachesTheApplication)
{
    PlatformEventRouter router;
    Menu menu;
    menu.ClaimsPresses = true;
    router.AddConsumer("menu", [&menu](const SDL_Event& event) {
        ++menu.Heard;
        return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    });
    InputFrame frame;

    EXPECT_TRUE(router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN), frame, nullptr));
    EXPECT_TRUE(router.Route(Pointer(SDL_EVENT_MOUSE_MOTION), frame, nullptr))
        << "a move the menu let fall through reached the world under its drag";
    EXPECT_TRUE(router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_UP), frame, nullptr));
    EXPECT_EQ(menu.Heard, 3);
}

TEST(PlatformEventRouter, AGestureLastsUntilItsLastButtonIsReleased)
{
    PlatformEventRouter router;
    InputFrame frame;
    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, 1, SDL_BUTTON_LEFT), frame, nullptr);
    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, 1, SDL_BUTTON_RIGHT), frame, nullptr);
    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_UP, 1, SDL_BUTTON_LEFT), frame, nullptr);
    EXPECT_TRUE(router.HasGesture(1));
    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_UP, 1, SDL_BUTTON_RIGHT), frame, nullptr);
    EXPECT_FALSE(router.HasGesture(1));
}

TEST(PlatformEventRouter, EachWindowHasItsOwnGesture)
{
    PlatformEventRouter router;
    Menu menu;
    menu.AddTo(router);
    InputFrame frame;

    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, 1), frame, nullptr);
    menu.Heard = 0;
    EXPECT_TRUE(router.Route(Pointer(SDL_EVENT_MOUSE_MOTION, 2), frame, nullptr));
    EXPECT_EQ(menu.Heard, 1) << "a drag in one window took another window's pointer";
}

TEST(PlatformEventRouter, ALostFocusEndsTheGestureAndTellsItsOwner)
{
    PlatformEventRouter router;
    Menu menu;
    menu.ClaimsPresses = true;
    menu.AddTo(router);
    InputFrame frame;

    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN), frame, nullptr);
    (void)router.Route(WindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST), frame, nullptr);
    EXPECT_FALSE(router.HasGesture(1));
    ASSERT_EQ(menu.Ended.size(), 1u) << "the menu was left holding a press that will never be released";
    EXPECT_EQ(menu.Ended[0], 1u);

    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, 2), frame, nullptr);
    (void)router.Route(WindowEvent(SDL_EVENT_WINDOW_HIDDEN, 2), frame, nullptr);
    EXPECT_FALSE(router.HasGesture(2));
}

TEST(PlatformEventRouter, AHandedGestureIsTheApplicationsToTheEnd)
{
    PlatformEventRouter router;
    Menu menu;
    menu.ClaimsPresses = true;
    menu.AddTo(router);
    InputFrame frame;

    EXPECT_FALSE(router.HandGestureToApplication(1)) << "a gesture nobody began was handed on";
    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN), frame, nullptr);
    EXPECT_TRUE(router.HandGestureToApplication(1));
    EXPECT_EQ(menu.Ended.size(), 1u);
    EXPECT_TRUE(router.ApplicationHasGesture(1));

    menu.Heard = 0;
    EXPECT_FALSE(router.Route(Pointer(SDL_EVENT_MOUSE_MOTION), frame, nullptr));
    EXPECT_FALSE(router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_UP), frame, nullptr));
    EXPECT_EQ(menu.Heard, 0);
    EXPECT_FALSE(router.HasGesture(1));
}

TEST(PlatformEventRouter, AMoveWithoutTheGesturesButtonShowsItsReleaseWasMissed)
{
    // The platform reports which buttons are down with every move. A gesture
    // whose button a move no longer reports was let go somewhere its release
    // never reached, and its owner is owed that release before the move.
    PlatformEventRouter router;
    InputFrame frame;
    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, 1, SDL_BUTTON_LEFT), frame, nullptr);
    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, 1, SDL_BUTTON_RIGHT), frame, nullptr);

    SDL_Event held = Pointer(SDL_EVENT_MOUSE_MOTION);
    held.motion.state = SDL_BUTTON_LMASK | SDL_BUTTON_RMASK;
    EXPECT_TRUE(router.ReleasesMissedBefore(held).empty()) << "a release was made up for buttons still held";

    SDL_Event leftLetGo = Pointer(SDL_EVENT_MOUSE_MOTION);
    leftLetGo.motion.state = SDL_BUTTON_RMASK;
    leftLetGo.motion.x = 40.0f;
    leftLetGo.motion.y = 30.0f;
    const std::vector<SDL_Event> missed = router.ReleasesMissedBefore(leftLetGo);
    ASSERT_EQ(missed.size(), 1u);
    EXPECT_EQ(missed[0].type, SDL_EVENT_MOUSE_BUTTON_UP);
    EXPECT_EQ(missed[0].button.button, SDL_BUTTON_LEFT);
    EXPECT_EQ(missed[0].button.windowID, 1u);
    EXPECT_EQ(missed[0].button.x, 40.0f) << "the release is placed where the pointer was found";

    for (const SDL_Event& release : missed)
        (void)router.Route(release, frame, nullptr);
    EXPECT_TRUE(router.HasGesture(1)) << "the right button is still held";
    EXPECT_FALSE(frame.IsMouseButtonDown(SDL_BUTTON_LEFT)) << "the snapshot still holds the missed button";
}

TEST(PlatformEventRouter, OnlyAMoveInTheGesturesWindowSpeaksForIt)
{
    PlatformEventRouter router;
    InputFrame frame;
    (void)router.Route(Pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, 1), frame, nullptr);
    EXPECT_TRUE(router.ReleasesMissedBefore(Pointer(SDL_EVENT_MOUSE_MOTION, 2)).empty());
    EXPECT_TRUE(router.ReleasesMissedBefore(Pointer(SDL_EVENT_MOUSE_BUTTON_UP, 1)).empty());
}
