#include "UiRuntime.h"

#include <platform/SdlWindowService.h>
#include <ui/UiSurfacePlacement.h>

#include "rml/RmlInputBridge.h"

#include <RmlUi/Core.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>

namespace
{
    // A window point in the surface's own pixels. Inside says whether the
    // point lies within the placement; the coordinates are meaningful either
    // way, because a drag that started inside is delivered wherever it went.
    struct SurfacePoint
    {
        int X = 0;
        int Y = 0;
        bool Inside = false;
    };

    SurfacePoint ToSurface(const std::optional<Rect2d>& placement, RenderExtent size,
                           float windowX, float windowY)
    {
        if (!placement.has_value())
            return { static_cast<int>(windowX), static_cast<int>(windowY), true };
        const Rect2d& rect = *placement;
        if (rect.Size.X <= 0.0f || rect.Size.Y <= 0.0f)
            return {};
        const float sx = static_cast<float>(size.Width) / rect.Size.X;
        const float sy = static_cast<float>(size.Height) / rect.Size.Y;
        const float x = (windowX - rect.Position.X) * sx;
        const float y = (windowY - rect.Position.Y) * sy;
        const Vec2d windowPoint{ windowX, windowY };
        return { static_cast<int>(x), static_cast<int>(y),
                 MapWindowPointToSurface(windowPoint, rect, size).has_value() };
    }
}

bool UiRuntime::ProcessPlatformEvent(const SDL_Event& event)
{
    if (!Ready)
        return false;

    // Topmost first, and the first surface to consume ends it. Only the event's
    // window's surfaces are asked: another window's surface is not under this
    // pointer at all.
    const std::uint32_t eventWindow = SdlEventWindowId(event);
    const bool pointer = event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                      || event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL;
    // A surface holding a press hears its window's pointer alone until let go.
    Surface* holder = pointer ? PointerHolder(eventWindow) : nullptr;
    const std::vector<std::size_t>& order = InputOrder;
    for (auto it = order.begin(); it != order.end(); ++it)
    {
        Surface& surface = Surfaces[*it];
        if (!surface.Live || surface.Context == nullptr)
            continue;
        if (holder != nullptr && &surface != holder)
            continue;
        if (surface.InputPolicy == UiSurfaceInputPolicy::Disabled)
            continue;
        if (!Hears(surface, eventWindow))
            continue;
        TextInput->SetWindow(WindowOf(surface));
        const bool keys = surface.InputPolicy == UiSurfaceInputPolicy::Full;

        Rml::Context& context = *surface.Context;
        const int modifiers = ToRmlKeyModifiers(SDL_GetModState());
        // An action the document raises against a callback nobody declared is
        // reported by the engine while the event is dispatched, which is here.
        const UiDiagnosticLog::Scope dispatching(
            Diagnostics, SurfaceAttribution(static_cast<std::size_t>(&surface - Surfaces.data())));

        // Every Process* here returns true when the UI is NOT interacting, so
        // consumption is the negation. Reading it the other way round would
        // hand every event to the UI and none to the game.
        switch (event.type)
        {
        case SDL_EVENT_MOUSE_MOTION:
        {
            const SurfacePoint at =
                ToSurface(surface.Placement, surface.Size, event.motion.x, event.motion.y);
            if (!at.Inside && surface.HeldButtons == 0)
            {
                // Left the placement with nothing held: the document is told
                // once, so nothing stays hovered.
                if (surface.PointerInside)
                {
                    surface.PointerInside = false;
                    (void)context.ProcessMouseLeave();
                }
                break;
            }
            surface.PointerX = at.X;
            surface.PointerY = at.Y;
            surface.PointerInside = at.Inside;
            if (!context.ProcessMouseMove(at.X, at.Y, modifiers) || surface.HeldButtons != 0)
            {
                // The surfaces below never see this move, so nothing on them
                // stays hovered under a pointer that is no longer theirs.
                for (auto below = std::next(it); below != order.end(); ++below)
                {
                    Surface& under = Surfaces[*below];
                    if (under.Live && under.Context != nullptr && under.PointerInside && under.HeldButtons == 0
                        && Hears(under, eventWindow))
                    {
                        under.PointerInside = false;
                        (void)under.Context->ProcessMouseLeave();
                    }
                }
                return true;
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const int button = ToRmlMouseButton(event.button.button);
            if (button < 0)
                break;
            const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            const SurfacePoint at =
                ToSurface(surface.Placement, surface.Size, event.button.x, event.button.y);
            // A press lands only inside, unless the surface already holds one;
            // a release goes to whoever holds the pointer, wherever it is now.
            const bool held = surface.HeldButtons != 0;
            if (!at.Inside && !held)
            {
                if (down)
                    DropFocus(context);
                break;
            }
            // Re-stated, because a press with no preceding move -- a synthetic
            // event, a tap -- would otherwise be resolved against wherever the
            // pointer happened to be last.
            surface.PointerX = at.X;
            surface.PointerY = at.Y;
            surface.PointerInside = at.Inside;
            (void)context.ProcessMouseMove(at.X, at.Y, modifiers);
            const bool free = down ? context.ProcessMouseButtonDown(button, modifiers)
                                   : context.ProcessMouseButtonUp(button, modifiers);
            const std::uint32_t bit = 1u << static_cast<std::uint32_t>(button);
            if (down && !free)
                surface.HeldButtons |= bit;
            else if (!down)
                surface.HeldButtons &= ~bit;
            if (down && free && !held)
                DropFocus(context);
            if (!free || held)
            {
                // The surfaces below never see this press, so they are told
                // the person has moved on here rather than left focused.
                if (down && !held)
                {
                    for (auto below = std::next(it); below != order.end(); ++below)
                    {
                        Surface& under = Surfaces[*below];
                        if (under.Live && under.Context != nullptr && Hears(under, eventWindow))
                            DropFocus(*under.Context);
                    }
                }
                return true;
            }
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL:
        {
            const SurfacePoint at = ToSurface(surface.Placement, surface.Size,
                                              event.wheel.mouse_x, event.wheel.mouse_y);
            if (!at.Inside)
                break;
            // Consumed only where something scrolled, a listener stopped it, or an
            // element holds it (`overscroll-behavior: contain`, `wheel-zoom`).
            if (!context.ProcessMouseWheel(Rml::Vector2f(-event.wheel.x, -event.wheel.y), modifiers))
                return true;
            break;
        }

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            if (!keys)
                break;
            const Rml::Input::KeyIdentifier key = ToRmlKey(event.key.scancode);
            if (key == Rml::Input::KI_UNKNOWN)
                break;
            const bool free = event.type == SDL_EVENT_KEY_DOWN
                ? context.ProcessKeyDown(key, modifiers)
                : context.ProcessKeyUp(key, modifiers);
            // Only claimed while a field is actually taking text. A menu that
            // swallowed every key would take the console and the pause key with
            // it, and neither belongs to the menu.
            if (!free || TextInput->IsTextInputActive())
                return true;
            break;
        }

        case SDL_EVENT_TEXT_INPUT:
            if (!keys)
                break;
            // Composed characters, from the platform. Never reassembled from
            // keycodes, which is wrong in every locale but the author's.
            if (TextInput->IsTextInputActive())
            {
                (void)context.ProcessTextInput(Rml::String(event.text.text));
                return true;
            }
            break;

        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            surface.PointerInside = false;
            (void)context.ProcessMouseLeave();
            break;

        default:
            break;
        }
    }

    return holder != nullptr;
}

UiRuntime::Surface* UiRuntime::PointerHolder(std::uint32_t platformWindowId)
{
    for (Surface& surface : Surfaces)
        if (surface.Live && surface.Context != nullptr && surface.HeldButtons != 0
            && surface.InputPolicy != UiSurfaceInputPolicy::Disabled && Hears(surface, platformWindowId))
            return &surface;
    return nullptr;
}

void UiRuntime::EndPress(Surface& surface)
{
    // Released with nothing under the pointer: the active element is reset
    // without a click, and a drag ends where it last was.
    if (surface.HeldButtons != 0 && surface.Context != nullptr)
    {
        Rml::Context& context = *surface.Context;
        (void)context.ProcessMouseLeave();
        for (std::uint32_t button = 0; button < 32; ++button)
            if ((surface.HeldButtons & (1u << button)) != 0)
                (void)context.ProcessMouseButtonUp(static_cast<int>(button), 0);
        (void)context.ProcessMouseLeave();
        surface.PointerInside = false;
    }
    surface.HeldButtons = 0;
}

void UiRuntime::EndPointerGesture(std::uint32_t platformWindowId)
{
    for (Surface& surface : Surfaces)
        if (surface.Live && Hears(surface, platformWindowId))
            EndPress(surface);
}

void UiRuntime::RebuildInputOrder()
{
    // A window's own surfaces draw over everything its host composites, offscreen
    // pictures included, so they are asked first; within each, reverse creation
    // order is the z-order a host expects without having to state one.
    InputOrder.resize(Surfaces.size());
    for (std::size_t i = 0; i < InputOrder.size(); ++i)
        InputOrder[i] = InputOrder.size() - 1 - i;
    std::stable_partition(InputOrder.begin(), InputOrder.end(), [this](std::size_t index) {
        return Surfaces[index].Destination == UiSurfaceDestination::Window;
    });
}

UiInputCapture UiRuntime::Capture() const
{
    return CaptureOver(std::nullopt);
}

UiInputCapture UiRuntime::Capture(std::uint32_t platformWindowId) const
{
    return CaptureOver(platformWindowId);
}

UiInputCapture UiRuntime::CaptureOver(std::optional<std::uint32_t> window) const
{
    UiInputCapture capture;
    if (!Ready)
        return capture;

    bool anyTakesKeys = false;
    for (const Surface& surface : Surfaces)
    {
        if (!surface.Live || surface.Context == nullptr)
            continue;
        if (surface.InputPolicy == UiSurfaceInputPolicy::Disabled)
            continue;
        if (window.has_value() && !Hears(surface, *window))
            continue;
        // The keyboard is taken on the window whose field has text input, not
        // on every window that has a surface able to take keys.
        anyTakesKeys |= surface.InputPolicy == UiSurfaceInputPolicy::Full
            && (!window.has_value() || WindowOf(surface) == TextInput->ActiveWindow());
        // A surface the pointer is not over is not interacting with it,
        // whatever hover state its context still remembers; one it holds a
        // press in is, wherever the pointer went.
        if ((surface.PointerInside || surface.HeldButtons != 0) && surface.Context->IsMouseInteracting())
            capture.Mouse = true;
    }

    // Keyboard capture is a text field having focus, and nothing wider. A
    // screen merely being open does not take the keyboard: a HUD is open for
    // the whole game, and a pause menu still has to let the console key
    // through. What suppresses gameplay controls is an InputContextLease the
    // host takes, which is a decision this layer does not get to make.
    capture.Keyboard = anyTakesKeys && TextInput != nullptr && TextInput->IsTextInputActive();
    return capture;
}

void UiRuntime::Navigate(UiSurfaceId surface, UiNavigation direction)
{
    Surface* slot = ResolveSurface(surface);
    if (slot == nullptr || slot->Context == nullptr)
        return;

    // Expressed as the keys the document engine's own focus and spatial
    // navigation already understand. What decided to send it -- a stick, a
    // remapped button, a key -- was settled by the host's action mapping before
    // this was called, which is the whole point of not reading devices here.
    Rml::Input::KeyIdentifier key = Rml::Input::KI_UNKNOWN;
    int modifiers = 0;
    switch (direction)
    {
    case UiNavigation::Up:       key = Rml::Input::KI_UP; break;
    case UiNavigation::Down:     key = Rml::Input::KI_DOWN; break;
    case UiNavigation::Left:     key = Rml::Input::KI_LEFT; break;
    case UiNavigation::Right:    key = Rml::Input::KI_RIGHT; break;
    case UiNavigation::Next:     key = Rml::Input::KI_TAB; break;
    case UiNavigation::Previous: key = Rml::Input::KI_TAB;
                                 modifiers = Rml::Input::KM_SHIFT; break;
    case UiNavigation::Accept:   key = Rml::Input::KI_RETURN; break;
    case UiNavigation::Cancel:   key = Rml::Input::KI_ESCAPE; break;
    }
    if (key == Rml::Input::KI_UNKNOWN)
        return;

    (void)slot->Context->ProcessKeyDown(key, modifiers);
    (void)slot->Context->ProcessKeyUp(key, modifiers);
}

// Pressing empty space or losing the keyboard is the person moving on, and the
// document engine only moves focus to something a press landed on.
void UiRuntime::DropFocus(Rml::Context& context)
{
    Rml::Element* focus = context.GetFocusElement();
    if (focus != nullptr && focus != context.GetRootElement())
        focus->Blur();
}
