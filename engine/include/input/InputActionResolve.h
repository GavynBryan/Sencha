#pragma once

#include <input/InputAction.h>
#include <input/InputBindings.h>
#include <input/InputFrame.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// Action resolution
//
// Pure, allocation-free translation of device state into action values. No
// world, no schedule, no SDL: a scenario is an InputFrame, a compiled profile,
// and an active-context set, which is what lets the edge and tick rules be
// tested exhaustively without booting anything.
//
// Two clocks read the same devices over different spans. The presentation
// clock resolves once per rendered frame; the simulation clock resolves once
// per fixed tick, which may be zero, one, or several times for the same frame.
// Each keeps its own latch so neither can consume the other's impulses.
//=============================================================================

// What is held, and where the analog controls are resting.
//
// Captured once per rendered frame so every tick of that frame resolves against
// the same devices, and so simulation never needs the platform's frame at all.
struct InputDeviceSnapshot
{
    std::array<std::uint64_t, kInputScancodeCount / 64> KeysHeld{};
    std::uint64_t MouseButtonsHeld = 0;
    std::uint32_t GamepadButtonsHeld = 0;
    std::array<float, kInputGamepadAxisCount> GamepadAxes{};

    void CaptureFrom(const InputFrame& frame);

    [[nodiscard]] bool IsHeld(InputControl control) const;
    [[nodiscard]] float Axis(GamepadAxis axis) const;
};

// Device transitions and motion a clock has not consumed yet.
//
// Latching is what makes an impulse survive a frame that runs no fixed tick,
// and what stops a catch-up burst from replaying one press on every tick: the
// first tick empties the latch, so ticks after it see held state only.
struct InputEdgeLatch
{
    std::array<std::uint64_t, kInputScancodeCount / 64> KeysPressed{};
    std::array<std::uint64_t, kInputScancodeCount / 64> KeysReleased{};
    std::uint64_t MouseButtonsPressed = 0;
    std::uint64_t MouseButtonsReleased = 0;
    std::uint32_t GamepadButtonsPressed = 0;
    std::uint32_t GamepadButtonsReleased = 0;
    float MotionX = 0.0f;
    float MotionY = 0.0f;
    float Wheel = 0.0f;

    void Accumulate(const InputFrame& frame);
    void Clear();

    [[nodiscard]] bool WasPressed(InputControl control) const;
    [[nodiscard]] bool WasReleased(InputControl control) const;
};

// What one clock carries between its passes.
struct InputClockState
{
    InputEdgeLatch Latch;
    // Held state after the previous pass, by dense action index. A context that
    // goes away while it was holding an action owes that action a release edge,
    // and this is what detects it.
    std::vector<std::uint8_t> HeldPrevious;

    // Threshold state per binding, for the bindings that turn an analog control
    // into a button. An analog control produces no device edge, so its crossing
    // has to be noticed here.
    //
    // Tracked for every binding, active context or not: the crossing is a fact
    // about the physical control, and contexts filter edges rather than create
    // them. Without that, activating a context over an already-pulled trigger
    // would fire a press the player never made -- the same rule the keyboard
    // path already keeps.
    std::vector<std::uint8_t> AnalogState;
};

// Fold this rendered frame's device transitions and motion into both clocks,
// then clear the frame's edge vectors.
//
// Runs once per rendered frame, before any fixed tick. Draining here rather
// than after the first tick is what lets the simulation latch own the
// zero-tick guarantee for motion and wheel impulses too, not just for buttons.
void AccumulateInputFrame(InputFrame& frame,
                          InputDeviceSnapshot& devices,
                          InputClockState& presentationClock,
                          InputClockState& simulationClock);

// Resolve every action for one clock and empty that clock's latch.
//
// `contextActive` is indexed by context, parallel to profile.Contexts.
// `out` must be sized to profile.ActionCount().
void ResolveInputActions(const BoundInputProfile& profile,
                         std::span<const std::uint8_t> contextActive,
                         const InputDeviceSnapshot& devices,
                         InputClockState& clock,
                         std::span<InputActionValue> out);
