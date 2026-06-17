#pragma once

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
};

struct PointerUpEvent
{
    ImVec2 Position;
    MouseButton Button;
    ModifierFlags Modifiers;
};

struct PointerMoveEvent
{
    ImVec2 Position;
    ImVec2 Delta;
    ModifierFlags Modifiers;
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
