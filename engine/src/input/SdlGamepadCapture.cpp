#include <input/SdlGamepadCapture.h>

#include <input/InputControl.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cmath>

namespace
{
// SDL reports sticks over the signed 16-bit range and triggers over the
// positive half of it.
constexpr float kAxisScale = 1.0f / 32767.0f;

bool MapButton(SDL_GamepadButton sdlButton, GamepadButton& out)
{
    switch (sdlButton)
    {
    case SDL_GAMEPAD_BUTTON_SOUTH:          out = GamepadButton::South; return true;
    case SDL_GAMEPAD_BUTTON_EAST:           out = GamepadButton::East; return true;
    case SDL_GAMEPAD_BUTTON_WEST:           out = GamepadButton::West; return true;
    case SDL_GAMEPAD_BUTTON_NORTH:          out = GamepadButton::North; return true;
    case SDL_GAMEPAD_BUTTON_BACK:           out = GamepadButton::Back; return true;
    case SDL_GAMEPAD_BUTTON_GUIDE:          out = GamepadButton::Guide; return true;
    case SDL_GAMEPAD_BUTTON_START:          out = GamepadButton::Start; return true;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:     out = GamepadButton::LeftStickClick; return true;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:    out = GamepadButton::RightStickClick; return true;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  out = GamepadButton::LeftShoulder; return true;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: out = GamepadButton::RightShoulder; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:        out = GamepadButton::DpadUp; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:      out = GamepadButton::DpadDown; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:      out = GamepadButton::DpadLeft; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:     out = GamepadButton::DpadRight; return true;
    default:                                return false;
    }
}

bool MapAxis(SDL_GamepadAxis sdlAxis, GamepadAxis& out)
{
    switch (sdlAxis)
    {
    case SDL_GAMEPAD_AXIS_LEFTX:         out = GamepadAxis::LeftX; return true;
    case SDL_GAMEPAD_AXIS_LEFTY:         out = GamepadAxis::LeftY; return true;
    case SDL_GAMEPAD_AXIS_RIGHTX:        out = GamepadAxis::RightX; return true;
    case SDL_GAMEPAD_AXIS_RIGHTY:        out = GamepadAxis::RightY; return true;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  out = GamepadAxis::LeftTrigger; return true;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: out = GamepadAxis::RightTrigger; return true;
    default:                             return false;
    }
}
}

SdlGamepadCapture::SdlGamepadCapture()
{
    SubsystemReady = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
}

SdlGamepadCapture::~SdlGamepadCapture()
{
    for (const OpenPad& pad : Pads)
        SDL_CloseGamepad(static_cast<SDL_Gamepad*>(pad.Handle));
    Pads.clear();
    if (SubsystemReady)
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void SdlGamepadCapture::Open(std::uint32_t instanceId, InputFrame& frame)
{
    const auto existing = std::find_if(Pads.begin(), Pads.end(),
        [instanceId](const OpenPad& pad) { return pad.InstanceId == instanceId; });
    if (existing != Pads.end())
        return;

    SDL_Gamepad* handle = SDL_OpenGamepad(static_cast<SDL_JoystickID>(instanceId));
    if (handle == nullptr)
        return;

    Pads.push_back(OpenPad{ instanceId, handle });
    frame.GamepadConnected = true;
}

void SdlGamepadCapture::Close(std::uint32_t instanceId, InputFrame& frame)
{
    const auto existing = std::find_if(Pads.begin(), Pads.end(),
        [instanceId](const OpenPad& pad) { return pad.InstanceId == instanceId; });
    if (existing == Pads.end())
        return;

    SDL_CloseGamepad(static_cast<SDL_Gamepad*>(existing->Handle));
    Pads.erase(existing);
    frame.GamepadConnected = !Pads.empty();

    // A pad unplugged mid-press never sends its button-up, and its sticks stop
    // reporting wherever they were pushed.
    if (Pads.empty())
    {
        std::uint32_t held = frame.GamepadButtonsHeld;
        while (held != 0)
        {
            const auto bit = static_cast<std::uint32_t>(std::countr_zero(held));
            held &= held - 1;
            frame.GamepadButtonsReleased.push_back(bit);
        }
        frame.GamepadButtonsHeld = 0;
        frame.GamepadAxes.fill(0.0f);
    }
}

void SdlGamepadCapture::OpenConnected(InputFrame& frame)
{
    if (!SubsystemReady)
        return;

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids == nullptr)
        return;
    for (int i = 0; i < count; ++i)
        Open(static_cast<std::uint32_t>(ids[i]), frame);
    SDL_free(ids);
}

bool SdlGamepadCapture::Accept(InputFrame& frame, const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_GAMEPAD_ADDED:
        Open(static_cast<std::uint32_t>(event.gdevice.which), frame);
        return true;

    case SDL_EVENT_GAMEPAD_REMOVED:
        Close(static_cast<std::uint32_t>(event.gdevice.which), frame);
        return true;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    {
        GamepadButton button{};
        if (!MapButton(static_cast<SDL_GamepadButton>(event.gbutton.button), button))
            return false;
        const auto index = static_cast<std::uint32_t>(button);
        if (!frame.IsGamepadButtonDown(index))
            frame.GamepadButtonsPressed.push_back(index);
        frame.SetGamepadButtonHeld(index, true);
        return true;
    }

    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    {
        GamepadButton button{};
        if (!MapButton(static_cast<SDL_GamepadButton>(event.gbutton.button), button))
            return false;
        const auto index = static_cast<std::uint32_t>(button);
        if (frame.IsGamepadButtonDown(index))
            frame.GamepadButtonsReleased.push_back(index);
        frame.SetGamepadButtonHeld(index, false);
        return true;
    }

    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    {
        GamepadAxis axis{};
        if (!MapAxis(static_cast<SDL_GamepadAxis>(event.gaxis.axis), axis))
            return false;

        // Last writer wins. With two pads open they fight over the same axis,
        // which is predictable and self-correcting; taking the larger magnitude
        // instead would leave the axis stuck, since a stick returning to centre
        // reports a smaller value than the one already recorded.
        frame.SetGamepadAxis(
            axis, std::clamp(static_cast<float>(event.gaxis.value) * kAxisScale, -1.0f, 1.0f));
        return true;
    }

    default:
        return false;
    }
}
