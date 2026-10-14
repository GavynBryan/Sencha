#pragma once

#include <input/InputControl.h>
#include <input/UiInputCapture.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

//=============================================================================
// InputFrame
//
// Per-wallclock-frame snapshot of raw device state, captured by the platform
// input system during the PumpPlatform phase.
//
// Gameplay does not read this. The input mapper turns it into actions once per
// frame, and simulation reads those; what is left here serves the platform
// layer, editor viewports, debug tooling, and a rebinding screen that needs to
// know which physical control moved.
//
// Button transitions are kept in one ordered log, in the order the platform
// reported them, until something drains it. The mapper drains it when it turns
// the transitions into action edges; FrameDriver drains it on the first fixed
// tick for hosts running no mapper.
//
// Keyboard: fixed-size bitset keyed by platform scancode. Bit set = held.
// Mouse: accumulated delta for this platform frame. Right-button hold is a
// typical capture toggle.
//
// Held state clears only on a real device release or through ReleaseAllHeld().
// Nothing is allowed to stop device events reaching this frame: PlatformEventRouter
// folds every event in before offering it to any consumer, precisely so a release
// edge cannot be thrown away by a surface that claimed the press. ReleaseAllHeld()
// is for the case the device genuinely stops reporting -- focus loss, where the
// key-up is never sent at all -- which SdlInputCapture::Accept handles itself.
//
// A surface consuming input does not hide it from here; it reports itself in
// UiCapture instead. A reader of mapped actions ignores that (an InputContextLease
// already decides what it hears); a reader of raw state below gates on it, because
// this snapshot faithfully contains the keystrokes someone typed into a console.
//
// This type is platform-agnostic — SDL / GLFW / Win32 capture adapters
// populate it identically.
//=============================================================================

static constexpr std::size_t kInputScancodeCount = 512;

// Gamepad state is normalized by the capture adapter into one abstract pad, so
// nothing above this layer knows which physical controller is plugged in. Sticks
// read [-1, 1] with negative Y up, matching the mouse's downward-positive
// convention; triggers read [0, 1].
enum class GamepadAxis : std::uint8_t
{
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count,
};

static constexpr std::size_t kInputGamepadAxisCount =
    static_cast<std::size_t>(GamepadAxis::Count);

// One button going down or up. Synthetic marks an up the device never sent --
// focus left with the button held -- which reaches gameplay as an interruption
// rather than as the player letting go.
struct InputTransition
{
    InputControl Control;
    bool Down = false;
    bool Synthetic = false;
};

static constexpr std::size_t kInputFrameTransitionCapacity = 64;

struct InputFrame
{
    std::array<uint64_t, kInputScancodeCount / 64> KeyHeld{};
    std::array<uint64_t, 1> MouseHeld{};

    // Past capacity a transition is dropped and counted; held state stays exact,
    // and the mapper reconciles against it.
    std::array<InputTransition, kInputFrameTransitionCapacity> Transitions{};
    std::uint8_t TransitionCount = 0;
    std::uint32_t DroppedTransitions = 0;

    float MouseDeltaX = 0.0f;
    float MouseDeltaY = 0.0f;
    float MouseWheelY = 0.0f;

    // Axes are positions, not displacement: they hold their value until the
    // device moves, so every tick of a catch-up frame reads the same stick.
    std::uint32_t GamepadButtonsHeld = 0;
    std::array<float, kInputGamepadAxisCount> GamepadAxes{};
    bool GamepadConnected = false;

    // Forget how far the pointer moved this frame.
    //
    // For displacement that is not input: the jump the cursor makes when
    // relative mouse mode is entered or left. Held buttons and keys are
    // untouched, because they genuinely are where they are -- and so is the
    // wheel, which a capture change does not move. Exactly as wide as its
    // reason, so a scroll aimed at the frame a menu closes still lands.
    void DropPointerMotion()
    {
        MouseDeltaX = 0.0f;
        MouseDeltaY = 0.0f;
    }

    bool QuitRequested = false;

    // Which devices a presentation surface owned while this frame's events were
    // routed. Set by the frame pump after routing; reset each frame by
    // SdlInputCapture::BeginFrame. Raw readers below gate on it -- see above.
    UiInputCapture UiCapture;

    // Release every held key and button as a synthetic release and drop pending
    // motion, for when device events stop arriving mid-press.
    //
    // Idempotent: with nothing held it produces no transitions, so a caller may
    // call it every frame for as long as the condition lasts rather than tracking
    // the transition itself.
    void ReleaseAllHeld()
    {
        for (std::size_t word = 0; word < KeyHeld.size(); ++word)
        {
            uint64_t bits = KeyHeld[word];
            while (bits != 0)
            {
                const auto bit = static_cast<uint32_t>(std::countr_zero(bits));
                bits &= bits - 1;
                Record({ InputControlSource::Key, static_cast<std::uint16_t>(word * 64 + bit) }, false, true);
            }
            KeyHeld[word] = 0;
        }

        uint64_t buttons = MouseHeld[0];
        while (buttons != 0)
        {
            const auto bit = static_cast<uint32_t>(std::countr_zero(buttons));
            buttons &= buttons - 1;
            Record({ InputControlSource::MouseButton, static_cast<std::uint16_t>(bit) }, false, true);
        }
        MouseHeld[0] = 0;

        std::uint32_t padButtons = GamepadButtonsHeld;
        while (padButtons != 0)
        {
            const auto bit = static_cast<uint32_t>(std::countr_zero(padButtons));
            padButtons &= padButtons - 1;
            Record({ InputControlSource::GamepadButton, static_cast<std::uint16_t>(bit) }, false, true);
        }
        GamepadButtonsHeld = 0;
        GamepadAxes.fill(0.0f);

        MouseDeltaX = 0.0f;
        MouseDeltaY = 0.0f;
        MouseWheelY = 0.0f;
    }

    void ClearTransitions() { TransitionCount = 0; }

    [[nodiscard]] std::span<const InputTransition> TransitionLog() const
    {
        return std::span(Transitions).first(TransitionCount);
    }

    // A button's new state, logged as a transition only when it changes.
    void SetButton(InputControl control, bool down)
    {
        if (!HasButton(control) || IsButtonDown(control) == down)
            return;
        Record(control, down, false);
        switch (control.Source)
        {
        case InputControlSource::Key:           SetKeyHeld(control.Index, down); break;
        case InputControlSource::MouseButton:   SetMouseButtonHeld(control.Index, down); break;
        case InputControlSource::GamepadButton: SetGamepadButtonHeld(control.Index, down); break;
        default: break;
        }
    }

    [[nodiscard]] static bool HasButton(InputControl control)
    {
        switch (control.Source)
        {
        case InputControlSource::Key:           return control.Index < kInputScancodeCount;
        case InputControlSource::MouseButton:   return control.Index < 64;
        case InputControlSource::GamepadButton: return control.Index < 32;
        default:                                return false;
        }
    }

    [[nodiscard]] bool IsButtonDown(InputControl control) const
    {
        switch (control.Source)
        {
        case InputControlSource::Key:           return IsKeyDown(control.Index);
        case InputControlSource::MouseButton:   return IsMouseButtonDown(control.Index);
        case InputControlSource::GamepadButton: return IsGamepadButtonDown(control.Index);
        default:                                return false;
        }
    }

    [[nodiscard]] bool IsKeyDown(uint32_t scancode) const
    {
        if (scancode >= kInputScancodeCount) return false;
        const std::size_t word = scancode / 64;
        const std::size_t bit = scancode % 64;
        return (KeyHeld[word] & (uint64_t{1} << bit)) != 0;
    }

    [[nodiscard]] bool IsMouseButtonDown(uint32_t button) const
    {
        if (button >= 64) return false;
        return (MouseHeld[0] & (uint64_t{1} << button)) != 0;
    }

    void SetKeyHeld(uint32_t scancode, bool held)
    {
        if (scancode >= kInputScancodeCount) return;
        const std::size_t word = scancode / 64;
        const std::size_t bit = scancode % 64;
        const uint64_t mask = uint64_t{1} << bit;
        if (held) KeyHeld[word] |= mask;
        else      KeyHeld[word] &= ~mask;
    }

    void SetMouseButtonHeld(uint32_t button, bool held)
    {
        if (button >= 64) return;
        const uint64_t mask = uint64_t{1} << button;
        if (held) MouseHeld[0] |= mask;
        else      MouseHeld[0] &= ~mask;
    }

    [[nodiscard]] bool IsGamepadButtonDown(uint32_t button) const
    {
        if (button >= 32) return false;
        return (GamepadButtonsHeld & (std::uint32_t{1} << button)) != 0;
    }

    void SetGamepadButtonHeld(uint32_t button, bool held)
    {
        if (button >= 32) return;
        const std::uint32_t mask = std::uint32_t{1} << button;
        if (held) GamepadButtonsHeld |= mask;
        else      GamepadButtonsHeld &= ~mask;
    }

    [[nodiscard]] float GetGamepadAxis(GamepadAxis axis) const
    {
        const auto index = static_cast<std::size_t>(axis);
        return index < GamepadAxes.size() ? GamepadAxes[index] : 0.0f;
    }

    void SetGamepadAxis(GamepadAxis axis, float value)
    {
        const auto index = static_cast<std::size_t>(axis);
        if (index < GamepadAxes.size())
            GamepadAxes[index] = value;
    }

private:
    void Record(InputControl control, bool down, bool synthetic)
    {
        if (TransitionCount == kInputFrameTransitionCapacity)
        {
            ++DroppedTransitions;
            return;
        }
        Transitions[TransitionCount++] = InputTransition{ control, down, synthetic };
    }
};
