#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

//=============================================================================
// InputFrame
//
// Per-wallclock-frame snapshot of input state. Captured by the platform input
// system during the PumpPlatform phase and consumed by simulation during
// fixed ticks and presentation during render.
//
// Edges (Pressed/Released) are drained on the FIRST fixed tick of the frame.
// If a frame runs zero ticks, edges persist into the next frame so impulses
// are never silently dropped. If a frame runs N ticks, ticks 2..N see an
// empty edge list and only observe held state.
//
// Keyboard: fixed-size bitset keyed by platform scancode. Bit set = held.
// Mouse: absolute position in window-local pixels plus accumulated delta
// for this platform frame. Right-button hold is a typical capture toggle.
//
// This type is platform-agnostic — SDL / GLFW / Win32 capture adapters
// populate it identically.
//=============================================================================

static constexpr std::size_t kInputScancodeCount = 512;

struct InputFrame
{
    std::array<uint64_t, kInputScancodeCount / 64> KeyHeld{};
    std::vector<uint32_t> KeysPressed;
    std::vector<uint32_t> KeysReleased;

    std::array<uint64_t, 1> MouseHeld{};
    std::vector<uint32_t> MouseButtonsPressed;
    std::vector<uint32_t> MouseButtonsReleased;

    float MouseX = 0.0f;
    float MouseY = 0.0f;
    float MouseDeltaX = 0.0f;
    float MouseDeltaY = 0.0f;
    float MouseWheelY = 0.0f;

    bool QuitRequested = false;
    bool FocusLost = false;

    uint64_t FrameIndex = 0;

    void Reset()
    {
        KeyHeld.fill(0);
        KeysPressed.clear();
        KeysReleased.clear();
        MouseHeld.fill(0);
        MouseButtonsPressed.clear();
        MouseButtonsReleased.clear();
        MouseDeltaX = 0.0f;
        MouseDeltaY = 0.0f;
        MouseWheelY = 0.0f;
        QuitRequested = false;
        FocusLost = false;
    }

    void ClearEdges()
    {
        KeysPressed.clear();
        KeysReleased.clear();
        MouseButtonsPressed.clear();
        MouseButtonsReleased.clear();
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
};
