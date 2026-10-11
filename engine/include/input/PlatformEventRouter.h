#pragma once

#include <input/InputFrame.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

union SDL_Event;

class SdlGamepadCapture;

//=============================================================================
// PlatformEventRouter
//
// Who sees a platform event, in what order, and what that order guarantees.
//
// The rule this type exists to hold: the canonical device snapshot is folded
// FIRST and UNCONDITIONALLY, before any consumer is offered the event. A
// consumer that claims an event hides it from later consumers -- never from the
// snapshot. A key-up that never reaches the snapshot leaves that key held
// forever, and no amount of compensating afterwards recovers the release edge
// that was thrown away.
//
// Consumers are offered the event highest priority first, and the first to
// return true ends the offer. Priority is the z-order of the surfaces involved:
// diagnostics above authored UI, authored UI above the application. A surface
// that consumed input reports it through UiInputCapture, which is how a reader
// of raw device state learns those keystrokes were not meant for it; a reader of
// mapped actions needs nothing, because an InputContextLease already answers it.
//
// Consumers are registered once at startup and run in registration order, so
// there is no per-event sorting and no priority number to keep in sync.
//
// It is also the one owner of each window's pointer gesture, from a press to
// the release of the last button held: whoever claimed the press hears the
// window's pointer alone until then (docs/ui/architecture.md, Input).
//=============================================================================
class PlatformEventRouter
{
public:
    // Returns true to claim the event, hiding it from every later consumer.
    using Consumer = std::function<bool(const SDL_Event&)>;
    // Told that the gesture it owns in a window ended without its release.
    using GestureEnded = std::function<void(std::uint32_t window)>;

    // Registered highest priority first. `name` is for diagnostics only.
    void AddConsumer(std::string_view name, Consumer consumer, GestureEnded gestureEnded = {});

    // The rest of the gesture in `window`, release included, is the
    // application's; its consumer is told it ended. False when none is open.
    bool HandGestureToApplication(std::uint32_t window);

    [[nodiscard]] bool HasGesture(std::uint32_t window) const;
    [[nodiscard]] bool ApplicationHasGesture(std::uint32_t window) const;

    [[nodiscard]] std::size_t ConsumerCount() const { return Consumers.size(); }
    [[nodiscard]] std::string_view ConsumerName(std::size_t index) const;

    // Folds the event into `frame` (and `gamepads`, when present), then offers
    // it to the consumers in order. Returns true if a consumer claimed it,
    // which is what the caller uses to skip its own fallback handling.
    //
    // The fold happens whatever the consumers do. That is the whole contract.
    bool Route(const SDL_Event& event, InputFrame& frame, SdlGamepadCapture* gamepads);

private:
    struct Entry
    {
        std::string Name;
        Consumer Handle;
        GestureEnded Ended;
    };

    static constexpr std::size_t kApplication = static_cast<std::size_t>(-1);

    struct Gesture
    {
        std::uint32_t Window = 0;
        std::size_t Owner = kApplication;
        std::uint32_t Buttons = 0;
    };

    bool RoutePointer(const SDL_Event& event, std::uint32_t window);
    [[nodiscard]] std::size_t Offer(const SDL_Event& event) const;
    void EndGesture(std::uint32_t window);
    [[nodiscard]] Gesture* Find(std::uint32_t window);
    [[nodiscard]] const Gesture* Find(std::uint32_t window) const;

    std::vector<Entry> Consumers;
    std::vector<Gesture> Gestures;
};
