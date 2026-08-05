#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

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
// Edges (Pressed/Released) persist until something drains them, so a frame that
// runs no fixed tick cannot silently drop an impulse. The mapper drains them
// when it folds them into its own per-clock latches; FrameDriver drains them on
// the first fixed tick for hosts running no mapper, which is also what keeps the
// edge lists from growing without bound.
//
// Keyboard: fixed-size bitset keyed by platform scancode. Bit set = held.
// Mouse: accumulated delta for this platform frame. Right-button hold is a
// typical capture toggle.
//
// Held state clears only on a real device release or through ReleaseAllHeld().
// Anything that stops device events reaching this frame mid-press -- focus
// loss, a UI layer taking capture -- owes a ReleaseAllHeld() call, because the
// matching key-up will never arrive and the key would stay held forever.
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

struct InputFrame
{
    std::array<uint64_t, kInputScancodeCount / 64> KeyHeld{};
    std::vector<uint32_t> KeysPressed;
    std::vector<uint32_t> KeysReleased;

    std::array<uint64_t, 1> MouseHeld{};
    std::vector<uint32_t> MouseButtonsPressed;
    std::vector<uint32_t> MouseButtonsReleased;

    float MouseDeltaX = 0.0f;
    float MouseDeltaY = 0.0f;
    float MouseWheelY = 0.0f;

    // Axes are positions, not displacement: they hold their value until the
    // device moves, so every tick of a catch-up frame reads the same stick.
    std::uint32_t GamepadButtonsHeld = 0;
    std::vector<uint32_t> GamepadButtonsPressed;
    std::vector<uint32_t> GamepadButtonsReleased;
    std::array<float, kInputGamepadAxisCount> GamepadAxes{};
    bool GamepadConnected = false;

    bool QuitRequested = false;

    // Release every held key and button as a release edge and drop pending
    // motion, for when device events stop arriving mid-press.
    //
    // Idempotent: with nothing held it produces no edges, so a caller may call
    // it every frame for as long as the condition lasts rather than tracking
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
                KeysReleased.push_back(static_cast<uint32_t>(word * 64) + bit);
            }
            KeyHeld[word] = 0;
        }

        uint64_t buttons = MouseHeld[0];
        while (buttons != 0)
        {
            const auto bit = static_cast<uint32_t>(std::countr_zero(buttons));
            buttons &= buttons - 1;
            MouseButtonsReleased.push_back(bit);
        }
        MouseHeld[0] = 0;

        std::uint32_t padButtons = GamepadButtonsHeld;
        while (padButtons != 0)
        {
            const auto bit = static_cast<uint32_t>(std::countr_zero(padButtons));
            padButtons &= padButtons - 1;
            GamepadButtonsReleased.push_back(bit);
        }
        GamepadButtonsHeld = 0;
        GamepadAxes.fill(0.0f);

        MouseDeltaX = 0.0f;
        MouseDeltaY = 0.0f;
        MouseWheelY = 0.0f;
    }

    void ClearEdges()
    {
        KeysPressed.clear();
        KeysReleased.clear();
        MouseButtonsPressed.clear();
        MouseButtonsReleased.clear();
        GamepadButtonsPressed.clear();
        GamepadButtonsReleased.clear();
    }

    // Take a key press edge. Bindings that run outside the fixed-tick drain,
    // such as engine-level window and pause keys in PumpPlatform, must consume
    // the edge they act on: edges survive frames that run no fixed tick, so a
    // handler that only tests for the press would fire again next frame.
    bool ConsumeKeyPressed(uint32_t scancode)
    {
        const auto first = std::remove(KeysPressed.begin(), KeysPressed.end(), scancode);
        if (first == KeysPressed.end())
            return false;
        KeysPressed.erase(first, KeysPressed.end());
        return true;
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
};
