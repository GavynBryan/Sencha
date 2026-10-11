#include <input/PlatformEventRouter.h>

#include <input/SdlGamepadCapture.h>
#include <input/SdlInputCapture.h>
#include <platform/SdlWindowService.h>

#include <SDL3/SDL.h>

#include <algorithm>

namespace
{
bool IsPointerEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return true;
    default:
        return false;
    }
}

// The window stops hearing the pointer, so no release will come to it.
bool EndsGestures(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_DESTROYED:
        return true;
    default:
        return false;
    }
}
} // namespace

void PlatformEventRouter::AddConsumer(std::string_view name, Consumer consumer, GestureEnded gestureEnded)
{
    if (!consumer)
        return;

    Consumers.push_back(Entry{ std::string(name), std::move(consumer), std::move(gestureEnded) });
}

std::string_view PlatformEventRouter::ConsumerName(std::size_t index) const
{
    if (index >= Consumers.size())
        return {};

    return Consumers[index].Name;
}

bool PlatformEventRouter::Route(const SDL_Event& event,
                                InputFrame& frame,
                                SdlGamepadCapture* gamepads)
{
    // First, and not inside any condition. See the header.
    SdlInputCapture::Accept(frame, event);
    if (gamepads != nullptr)
        gamepads->Accept(frame, event);

    const std::uint32_t window = SdlEventWindowId(event);
    if (IsPointerEvent(event))
        return RoutePointer(event, window);
    if (EndsGestures(event))
        EndGesture(window);
    return Offer(event) != kApplication;
}

bool PlatformEventRouter::RoutePointer(const SDL_Event& event, std::uint32_t window)
{
    Gesture* gesture = Find(window);
    if (gesture == nullptr)
    {
        const std::size_t claimant = Offer(event);
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            Gestures.push_back({ window, claimant, SDL_BUTTON_MASK(event.button.button) });
        return claimant != kApplication;
    }

    const std::size_t owner = gesture->Owner;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        gesture->Buttons |= SDL_BUTTON_MASK(event.button.button);
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        gesture->Buttons &= ~SDL_BUTTON_MASK(event.button.button);
        if (gesture->Buttons == 0)
            std::erase_if(Gestures, [window](const Gesture& open) { return open.Window == window; });
    }
    if (owner == kApplication)
        return false;
    (void)Consumers[owner].Handle(event);
    return true;
}

std::size_t PlatformEventRouter::Offer(const SDL_Event& event) const
{
    for (std::size_t i = 0; i < Consumers.size(); ++i)
        if (Consumers[i].Handle(event))
            return i;
    return kApplication;
}

void PlatformEventRouter::EndGesture(std::uint32_t window)
{
    const Gesture* gesture = Find(window);
    if (gesture == nullptr)
        return;
    const std::size_t owner = gesture->Owner;
    std::erase_if(Gestures, [window](const Gesture& open) { return open.Window == window; });
    if (owner != kApplication && Consumers[owner].Ended)
        Consumers[owner].Ended(window);
}

bool PlatformEventRouter::HandGestureToApplication(std::uint32_t window)
{
    Gesture* gesture = Find(window);
    if (gesture == nullptr)
        return false;
    const std::size_t owner = gesture->Owner;
    gesture->Owner = kApplication;
    if (owner != kApplication && Consumers[owner].Ended)
        Consumers[owner].Ended(window);
    return true;
}

std::vector<SDL_Event> PlatformEventRouter::ReleasesMissedBefore(const SDL_Event& event) const
{
    std::vector<SDL_Event> releases;
    if (event.type != SDL_EVENT_MOUSE_MOTION)
        return releases;
    const Gesture* gesture = Find(event.motion.windowID);
    if (gesture == nullptr)
        return releases;
    const std::uint32_t missed = gesture->Buttons & ~event.motion.state;
    for (std::uint8_t button = SDL_BUTTON_LEFT; button <= SDL_BUTTON_X2; ++button)
    {
        if ((missed & SDL_BUTTON_MASK(button)) == 0)
            continue;
        SDL_Event release{};
        release.type = SDL_EVENT_MOUSE_BUTTON_UP;
        release.button.timestamp = event.motion.timestamp;
        release.button.windowID = event.motion.windowID;
        release.button.which = event.motion.which;
        release.button.button = button;
        release.button.clicks = 1;
        release.button.x = event.motion.x;
        release.button.y = event.motion.y;
        releases.push_back(release);
    }
    return releases;
}

bool PlatformEventRouter::HasGesture(std::uint32_t window) const
{
    return Find(window) != nullptr;
}

bool PlatformEventRouter::ApplicationHasGesture(std::uint32_t window) const
{
    const Gesture* gesture = Find(window);
    return gesture != nullptr && gesture->Owner == kApplication;
}

PlatformEventRouter::Gesture* PlatformEventRouter::Find(std::uint32_t window)
{
    const auto found = std::ranges::find(Gestures, window, &Gesture::Window);
    return found != Gestures.end() ? &*found : nullptr;
}

const PlatformEventRouter::Gesture* PlatformEventRouter::Find(std::uint32_t window) const
{
    const auto found = std::ranges::find(Gestures, window, &Gesture::Window);
    return found != Gestures.end() ? &*found : nullptr;
}
