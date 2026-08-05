#pragma once

#include <input/GamepadDeviceSet.h>
#include <input/InputFrame.h>

#include <cstdint>

union SDL_Event;

//=============================================================================
// SdlGamepadCapture
//
// Opens connected gamepads and decodes their SDL events into a
// GamepadDeviceSet, which folds every open pad into the one abstract pad an
// InputFrame carries. Unlike the keyboard and mouse adapter this one is
// stateful, because a pad is a device that comes and goes and has to be held
// open to report anything.
//
// One player with a controller in each hand is still one player, and
// split-screen needs a resolved action set per player before it needs a device
// set per player. The fold and its rules live in GamepadDeviceSet; what is left
// here is SDL's event vocabulary and the lifetime of an open device.
//
// SDL's own mapping database normalizes layouts, so an Xbox, PlayStation, or
// generic pad all arrive as the same buttons and axes.
//=============================================================================
class SdlGamepadCapture
{
public:
    SdlGamepadCapture();
    ~SdlGamepadCapture();

    SdlGamepadCapture(const SdlGamepadCapture&) = delete;
    SdlGamepadCapture& operator=(const SdlGamepadCapture&) = delete;
    SdlGamepadCapture(SdlGamepadCapture&&) = delete;
    SdlGamepadCapture& operator=(SdlGamepadCapture&&) = delete;

    // Opens whatever is already plugged in. Separate from the constructor so a
    // caller can report the result; pads added later arrive as events.
    void OpenConnected(InputFrame& frame);

    // Folds one event into the frame. Returns true when the event was a gamepad
    // event this consumed.
    bool Accept(InputFrame& frame, const SDL_Event& event);

    [[nodiscard]] std::size_t OpenCount() const { return Devices.Count(); }
    [[nodiscard]] bool IsAvailable() const { return SubsystemReady; }

private:
    void Open(std::uint32_t instanceId, InputFrame& frame);
    void Close(std::uint32_t instanceId, InputFrame& frame);

    GamepadDeviceSet Devices;
    bool SubsystemReady = false;
};
