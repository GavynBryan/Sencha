#include <gtest/gtest.h>
#include <input/InputFrame.h>
#include <input/PlatformEventRouter.h>
#include <input/SdlInputCapture.h>

#include <SDL3/SDL.h>

#include <algorithm>
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
    EXPECT_NE(std::find(frame.KeysReleased.begin(), frame.KeysReleased.end(),
                        static_cast<uint32_t>(SDL_SCANCODE_W)),
              frame.KeysReleased.end())
        << "and must be reported as a release edge, not merely cleared";
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
