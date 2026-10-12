#pragma once

#include <input/InputFrame.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_events.h>

class SdlGamepadCapture;

// Folds every platform event into the device snapshot first and unconditionally,
// then offers it to consumers in registration order until one claims it, and owns
// each window's pointer gesture between them (docs/ui/architecture.md §6).
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

    // The releases a motion shows were missed: buttons the window's gesture
    // holds that the platform no longer reports down. Deliver them, in order,
    // before the motion itself.
    [[nodiscard]] std::vector<SDL_Event> ReleasesMissedBefore(const SDL_Event& event) const;

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
