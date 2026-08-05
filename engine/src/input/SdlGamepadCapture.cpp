#include <input/SdlGamepadCapture.h>

#include <input/InputControl.h>
#include <input/SdlInputCapture.h>

#include <SDL3/SDL.h>

#include <algorithm>
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
    for (void* handle : Devices.PlatformHandles())
        SDL_CloseGamepad(static_cast<SDL_Gamepad*>(handle));
    if (SubsystemReady)
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void SdlGamepadCapture::Open(std::uint32_t instanceId, InputFrame& frame)
{
    if (Devices.Contains(instanceId))
        return;

    SDL_Gamepad* handle = SDL_OpenGamepad(static_cast<SDL_JoystickID>(instanceId));
    if (handle == nullptr)
        return;

    Devices.Add(instanceId, handle);
    frame.GamepadConnected = true;
}

void SdlGamepadCapture::Close(std::uint32_t instanceId, InputFrame& frame)
{
    if (!Devices.Contains(instanceId))
        return;

    const std::uint32_t before = Devices.FoldButtons();
    SDL_CloseGamepad(static_cast<SDL_Gamepad*>(Devices.Remove(instanceId)));
    frame.GamepadConnected = !Devices.Empty();

    // A pad unplugged mid-press never sends its button-up, and its sticks stop
    // reporting wherever they were pushed. Dropping it from the fold releases
    // whatever only it was holding, and leaves alone whatever the pads still
    // plugged in are holding.
    Devices.PublishButtons(frame, before);
    Devices.PublishAxes(frame);
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
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    {
        GamepadButton button{};
        if (!MapButton(static_cast<SDL_GamepadButton>(event.gbutton.button), button))
            return false;

        const std::uint32_t before = Devices.FoldButtons();
        Devices.SetButton(static_cast<std::uint32_t>(event.gbutton.which),
                          static_cast<std::uint32_t>(button),
                          event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        Devices.PublishButtons(frame, before);
        return true;
    }

    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    {
        GamepadAxis axis{};
        if (!MapAxis(static_cast<SDL_GamepadAxis>(event.gaxis.axis), axis))
            return false;

        Devices.SetAxis(static_cast<std::uint32_t>(event.gaxis.which), axis,
                        std::clamp(static_cast<float>(event.gaxis.value) * kAxisScale, -1.0f, 1.0f));
        Devices.PublishAxes(frame);
        return true;
    }

    default:
        return false;
    }
}

std::optional<InputControl> InputControlFromSdlEvent(const SDL_Event& event, float axisThreshold)
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
        if (event.key.repeat)
            return std::nullopt;
        return InputControl{ InputControlSource::Key,
                             static_cast<std::uint16_t>(event.key.scancode) };

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return InputControl{ InputControlSource::MouseButton,
                             static_cast<std::uint16_t>(event.button.button) };

    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.y == 0.0f)
            return std::nullopt;
        return InputControl{ InputControlSource::MouseWheel, 0 };

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    {
        GamepadButton button{};
        if (!MapButton(static_cast<SDL_GamepadButton>(event.gbutton.button), button))
            return std::nullopt;
        return InputControl{ InputControlSource::GamepadButton,
                             static_cast<std::uint16_t>(button) };
    }

    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    {
        GamepadAxis axis{};
        if (!MapAxis(static_cast<SDL_GamepadAxis>(event.gaxis.axis), axis))
            return std::nullopt;
        if (std::fabs(static_cast<float>(event.gaxis.value) * kAxisScale) < axisThreshold)
            return std::nullopt;

        // A stick names the stick, not the one axis that happened to move: the
        // player pushing left means "the left stick", and a binding to half a
        // stick is not something this vocabulary can express anyway.
        switch (axis)
        {
        case GamepadAxis::LeftX:
        case GamepadAxis::LeftY:
            return InputControl{ InputControlSource::GamepadStick,
                                 static_cast<std::uint16_t>(GamepadStick::Left) };
        case GamepadAxis::RightX:
        case GamepadAxis::RightY:
            return InputControl{ InputControlSource::GamepadStick,
                                 static_cast<std::uint16_t>(GamepadStick::Right) };
        case GamepadAxis::LeftTrigger:
            return InputControl{ InputControlSource::GamepadTrigger,
                                 static_cast<std::uint16_t>(GamepadTrigger::Left) };
        case GamepadAxis::RightTrigger:
            return InputControl{ InputControlSource::GamepadTrigger,
                                 static_cast<std::uint16_t>(GamepadTrigger::Right) };
        case GamepadAxis::Count:
            break;
        }
        return std::nullopt;
    }

    default:
        return std::nullopt;
    }
}
