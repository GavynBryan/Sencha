#include "RmlInputBridge.h"

#include <SDL3/SDL_mouse.h>

namespace
{
using Rml::Input::KeyIdentifier;

struct KeyMapping
{
    SDL_Scancode Scancode;
    KeyIdentifier Key;
};

// Ordered by what a document actually uses: activation and navigation first,
// then editing, then the printable set a shortcut might name.
constexpr KeyMapping kKeyMap[] = {
    { SDL_SCANCODE_RETURN, Rml::Input::KI_RETURN },
    { SDL_SCANCODE_KP_ENTER, Rml::Input::KI_NUMPADENTER },
    { SDL_SCANCODE_ESCAPE, Rml::Input::KI_ESCAPE },
    { SDL_SCANCODE_TAB, Rml::Input::KI_TAB },
    { SDL_SCANCODE_SPACE, Rml::Input::KI_SPACE },

    { SDL_SCANCODE_LEFT, Rml::Input::KI_LEFT },
    { SDL_SCANCODE_RIGHT, Rml::Input::KI_RIGHT },
    { SDL_SCANCODE_UP, Rml::Input::KI_UP },
    { SDL_SCANCODE_DOWN, Rml::Input::KI_DOWN },
    { SDL_SCANCODE_HOME, Rml::Input::KI_HOME },
    { SDL_SCANCODE_END, Rml::Input::KI_END },
    { SDL_SCANCODE_PAGEUP, Rml::Input::KI_PRIOR },
    { SDL_SCANCODE_PAGEDOWN, Rml::Input::KI_NEXT },

    { SDL_SCANCODE_BACKSPACE, Rml::Input::KI_BACK },
    { SDL_SCANCODE_DELETE, Rml::Input::KI_DELETE },
    { SDL_SCANCODE_INSERT, Rml::Input::KI_INSERT },

    { SDL_SCANCODE_LSHIFT, Rml::Input::KI_LSHIFT },
    { SDL_SCANCODE_RSHIFT, Rml::Input::KI_RSHIFT },
    { SDL_SCANCODE_LCTRL, Rml::Input::KI_LCONTROL },
    { SDL_SCANCODE_RCTRL, Rml::Input::KI_RCONTROL },
    { SDL_SCANCODE_LALT, Rml::Input::KI_LMENU },
    { SDL_SCANCODE_RALT, Rml::Input::KI_RMENU },

    { SDL_SCANCODE_MINUS, Rml::Input::KI_OEM_MINUS },
    { SDL_SCANCODE_EQUALS, Rml::Input::KI_OEM_PLUS },
    { SDL_SCANCODE_COMMA, Rml::Input::KI_OEM_COMMA },
    { SDL_SCANCODE_PERIOD, Rml::Input::KI_OEM_PERIOD },
    { SDL_SCANCODE_SLASH, Rml::Input::KI_OEM_2 },
    { SDL_SCANCODE_SEMICOLON, Rml::Input::KI_OEM_1 },
    { SDL_SCANCODE_GRAVE, Rml::Input::KI_OEM_3 },
    { SDL_SCANCODE_LEFTBRACKET, Rml::Input::KI_OEM_4 },
    { SDL_SCANCODE_BACKSLASH, Rml::Input::KI_OEM_5 },
    { SDL_SCANCODE_RIGHTBRACKET, Rml::Input::KI_OEM_6 },
    { SDL_SCANCODE_APOSTROPHE, Rml::Input::KI_OEM_7 },
};
} // namespace

Rml::Input::KeyIdentifier ToRmlKey(SDL_Scancode scancode)
{
    // Letters and digits are contiguous in both enumerations, so a range check
    // beats thirty-six more table rows -- and a table that long is a table with
    // a typo in it.
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
    {
        return static_cast<KeyIdentifier>(
            Rml::Input::KI_A + (scancode - SDL_SCANCODE_A));
    }
    // SDL orders digits 1..9 then 0; the document engine orders 0..9.
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
    {
        return static_cast<KeyIdentifier>(
            Rml::Input::KI_1 + (scancode - SDL_SCANCODE_1));
    }
    if (scancode == SDL_SCANCODE_0)
        return Rml::Input::KI_0;
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
    {
        return static_cast<KeyIdentifier>(
            Rml::Input::KI_F1 + (scancode - SDL_SCANCODE_F1));
    }

    for (const KeyMapping& mapping : kKeyMap)
    {
        if (mapping.Scancode == scancode)
            return mapping.Key;
    }
    return Rml::Input::KI_UNKNOWN;
}

int ToRmlKeyModifiers(SDL_Keymod modifiers)
{
    int out = 0;
    if ((modifiers & SDL_KMOD_CTRL) != 0) out |= Rml::Input::KM_CTRL;
    if ((modifiers & SDL_KMOD_SHIFT) != 0) out |= Rml::Input::KM_SHIFT;
    if ((modifiers & SDL_KMOD_ALT) != 0) out |= Rml::Input::KM_ALT;
    if ((modifiers & SDL_KMOD_GUI) != 0) out |= Rml::Input::KM_META;
    if ((modifiers & SDL_KMOD_CAPS) != 0) out |= Rml::Input::KM_CAPSLOCK;
    if ((modifiers & SDL_KMOD_NUM) != 0) out |= Rml::Input::KM_NUMLOCK;
    if ((modifiers & SDL_KMOD_SCROLL) != 0) out |= Rml::Input::KM_SCROLLLOCK;
    return out;
}

int ToRmlMouseButton(std::uint8_t sdlButton)
{
    switch (sdlButton)
    {
    case SDL_BUTTON_LEFT:   return 0;
    case SDL_BUTTON_RIGHT:  return 1;
    case SDL_BUTTON_MIDDLE: return 2;
    default:                return -1;
    }
}
