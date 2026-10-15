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
    void CaptureButtonsFrom(const InputFrame& frame);
    void Apply(const InputTransition& transition);

    [[nodiscard]] bool IsHeld(InputControl control) const;
    [[nodiscard]] float Axis(GamepadAxis axis) const;
};

// Pointer and wheel displacement over some span: what a latch has accumulated,
// or the share of it one resolve pass takes.
struct InputMotionDelta
{
    float X = 0.0f;
    float Y = 0.0f;
    float Wheel = 0.0f;
};

// Displacement a clock has not consumed yet.
//
// Motion divides between passes rather than going to the first, so a heading
// turns in steps that match the simulated time each pass covers.
struct InputMotionLatch
{
    float X = 0.0f;
    float Y = 0.0f;
    float Wheel = 0.0f;

    void Accumulate(const InputFrame& frame);
    void Clear();

    // The displacement one pass takes when `shares` passes will split what is
    // latched now, this one included; zero and one both take the whole total.
    [[nodiscard]] InputMotionDelta Share(std::uint32_t shares) const;
    void Consume(const InputMotionDelta& taken);
};

// Controls a higher-priority context has already taken. A claimed control reads
// as absent to every context below, which is how one context shadows another's
// binding without any explicit block rule.
struct InputControlClaims
{
    std::array<std::uint64_t, kInputScancodeCount / 64> Keys{};
    std::uint64_t MouseButtons = 0;
    std::uint32_t GamepadButtons = 0;
    std::uint8_t Triggers = 0;
    std::uint8_t Sticks = 0;
    bool Motion = false;
    bool Wheel = false;

    void Add(InputControl control);
    [[nodiscard]] bool Has(InputControl control) const;
};

// What one clock carries between its passes.
struct InputClockState
{
    InputMotionLatch Motion;
    // Action edges attributed since this clock's last pass, in order. A frame
    // that runs no fixed tick leaves them here for the next one.
    InputActionEdgeList Pending;
    // Held state after the previous pass, by dense action index: where the
    // pass's edges start from.
    std::vector<std::uint8_t> HeldPrevious;
};

// Which actions are held as of the transitions turned into edges so far. One
// frame's devices and contexts give one answer, so both clocks share it.
struct InputEdgeAttribution
{
    InputDeviceSnapshot Devices;
    std::vector<std::uint8_t> ActionHeld;
    // Per binding, whether its trigger is past the threshold, active context or
    // not: a context switched on over a pulled trigger holds rather than presses.
    std::vector<std::uint8_t> AnalogAbove;
    // The claims in force at each context, rebuilt per frame.
    std::vector<InputControlClaims> ContextClaims;
    std::vector<std::uint8_t> Scratch;
};

// Once per rendered frame, before any fixed tick: the frame's transitions become
// action edges for both clocks (docs/gameplay/input.md, "Edges"). A null profile
// holds nothing, so everything held falls, interrupted.
void AttributeInputFrame(InputFrame& frame,
                         const BoundInputProfile* profile,
                         std::span<const std::uint8_t> contextActive,
                         InputEdgeAttribution& attribution,
                         InputClockState& presentationClock,
                         InputClockState& simulationClock);

// One clock's pass: values from the attributed devices, the clock's pending edges,
// and the flags that summarize them. `displacementShares` counts the passes
// splitting the latched motion, this one included; `outSampled`, when sized,
// receives the share from controls reporting a held position.
void ResolveInputActions(const BoundInputProfile& profile,
                         std::span<const std::uint8_t> contextActive,
                         const InputEdgeAttribution& attribution,
                         InputClockState& clock,
                         std::span<InputActionValue> out,
                         InputActionEdgeList& outEdges,
                         std::uint32_t displacementShares = 1,
                         std::span<InputActionValue> outSampled = {});

// The pass a clock publishes when no profile can resolve: no values, the clock's
// pending edges, and an interrupted fall for whatever they leave held.
void ReleaseInputActions(InputClockState& clock,
                         std::span<InputActionValue> out,
                         InputActionEdgeList& outEdges);

// Across a resume: input made while suspended is dropped; interruptions are kept.
void DiscardSuspendedInput(InputClockState& clock);
