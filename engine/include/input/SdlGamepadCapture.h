#pragma once

#include <input/InputFrame.h>

#include <cstdint>
#include <vector>

union SDL_Event;

//=============================================================================
// SdlGamepadCapture
//
// Opens connected gamepads and folds their events into an InputFrame. Unlike
// the keyboard and mouse adapter this one is stateful, because a pad is a
// device that comes and goes and has to be held open to report anything.
//
// Every open pad drives the same abstract pad in the frame: buttons or together,
// axes take the last value reported. One player with a controller in each hand
// is still one player, and split-screen needs a resolved action set per player
// before it needs a device set per player.
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

    [[nodiscard]] std::size_t OpenCount() const { return Pads.size(); }
    [[nodiscard]] bool IsAvailable() const { return SubsystemReady; }

private:
    struct OpenPad
    {
        std::uint32_t InstanceId = 0;
        void* Handle = nullptr;
    };

    void Open(std::uint32_t instanceId, InputFrame& frame);
    void Close(std::uint32_t instanceId, InputFrame& frame);

    std::vector<OpenPad> Pads;
    bool SubsystemReady = false;
};
