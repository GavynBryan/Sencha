#pragma once

#include "../viewport/ViewportId.h"

#include <SDL3/SDL_keycode.h>
#include <imgui.h>

#include <cstdint>
#include <variant>

enum class InputConsumed : uint8_t { Yes, No };
enum class MouseButton : uint8_t { Left, Right, Middle };

struct ModifierFlags
{
    bool Ctrl = false;
    bool Shift = false;
    bool Alt = false;
};

struct PointerDownEvent
{
    ImVec2 Position;
    MouseButton Button;
    ModifierFlags Modifiers;
    // The viewport this event belongs to, resolved once at the input boundary: the
    // viewport under the cursor, or — while a gesture holds capture — the viewport
    // it began in. Lets navigation and tools skip re-resolving, and keeps a drag on
    // its origin viewport even as the cursor crosses into another.
    ViewportId Viewport = {};
};

struct PointerUpEvent
{
    ImVec2 Position;
    MouseButton Button;
    ModifierFlags Modifiers;
    ViewportId Viewport = {};
};

struct PointerMoveEvent
{
    ImVec2 Position;
    ImVec2 Delta;
    ModifierFlags Modifiers;
    ViewportId Viewport = {};
};

struct WheelEvent
{
    ImVec2 Position;
    float Delta;
    ModifierFlags Modifiers;
};

struct KeyDownEvent
{
    SDL_Keycode Key;
    ModifierFlags Modifiers;
};

struct FocusLostEvent {};

// A viewport-space pointer event delivered to tools, edit sessions, and
// interactions. One struct rather than decomposed scalars so routing can't
// silently drop a field (the modifier-loss bug): add a field here and it reaches
// every handler without touching a single signature.
// (docs/architecture/hardening-and-consolidation.md W3.)
struct PointerEvent
{
    ImVec2        Position = {};
    ImVec2        Delta = {};                   // pointer motion (moves; {0,0} otherwise)
    MouseButton   Button = MouseButton::Left;   // the button, for down/up
    ModifierFlags Modifiers = {};
};

using InputEvent = std::variant<
    PointerDownEvent,
    PointerUpEvent,
    PointerMoveEvent,
    WheelEvent,
    KeyDownEvent,
    FocusLostEvent
>;
