#include <input/InputActionResolve.h>

#include <cmath>

namespace
{
// Controls a higher-priority context has already taken for this pass. A claimed
// control reads as absent to every context below, which is how one context
// shadows another's binding without any explicit block rule.
struct InputControlClaims
{
    std::array<std::uint64_t, kInputScancodeCount / 64> Keys{};
    std::uint64_t MouseButtons = 0;
    std::uint32_t GamepadButtons = 0;
    std::uint8_t Triggers = 0;
    std::uint8_t Sticks = 0;
    bool Motion = false;
    bool Wheel = false;

    void Add(InputControl control)
    {
        switch (control.Source)
        {
        case InputControlSource::Key:
            if (control.Index < kInputScancodeCount)
                Keys[control.Index / 64] |= std::uint64_t{ 1 } << (control.Index % 64);
            break;
        case InputControlSource::MouseButton:
            if (control.Index < 64)
                MouseButtons |= std::uint64_t{ 1 } << control.Index;
            break;
        case InputControlSource::GamepadButton:
            if (control.Index < 32)
                GamepadButtons |= std::uint32_t{ 1 } << control.Index;
            break;
        case InputControlSource::GamepadTrigger:
            if (control.Index < 8)
                Triggers |= static_cast<std::uint8_t>(1u << control.Index);
            break;
        case InputControlSource::GamepadStick:
            if (control.Index < 8)
                Sticks |= static_cast<std::uint8_t>(1u << control.Index);
            break;
        case InputControlSource::MouseMotion: Motion = true; break;
        case InputControlSource::MouseWheel:  Wheel = true; break;
        case InputControlSource::None:        break;
        }
    }

    [[nodiscard]] bool Has(InputControl control) const
    {
        switch (control.Source)
        {
        case InputControlSource::Key:
            return control.Index < kInputScancodeCount
                && (Keys[control.Index / 64] & (std::uint64_t{ 1 } << (control.Index % 64))) != 0;
        case InputControlSource::MouseButton:
            return control.Index < 64
                && (MouseButtons & (std::uint64_t{ 1 } << control.Index)) != 0;
        case InputControlSource::GamepadButton:
            return control.Index < 32
                && (GamepadButtons & (std::uint32_t{ 1 } << control.Index)) != 0;
        case InputControlSource::GamepadTrigger:
            return control.Index < 8 && (Triggers & (1u << control.Index)) != 0;
        case InputControlSource::GamepadStick:
            return control.Index < 8 && (Sticks & (1u << control.Index)) != 0;
        case InputControlSource::MouseMotion: return Motion;
        case InputControlSource::MouseWheel:  return Wheel;
        case InputControlSource::None:        return false;
        }
        return false;
    }
};

// Stick and trigger positions come from the device rather than from a latch:
// an axis holds its value until the stick moves, so every tick of a catch-up
// frame reads the same position rather than sharing one displacement.
void ReadGamepadAxes(const InputDeviceSnapshot& devices, InputControl control, float& x, float& y)
{
    if (control.Source == InputControlSource::GamepadTrigger)
    {
        x = devices.Axis(control.Index == static_cast<std::uint16_t>(GamepadTrigger::Right)
                             ? GamepadAxis::RightTrigger
                             : GamepadAxis::LeftTrigger);
        return;
    }

    const bool right = control.Index == static_cast<std::uint16_t>(GamepadStick::Right);
    x = devices.Axis(right ? GamepadAxis::RightX : GamepadAxis::LeftX);
    y = devices.Axis(right ? GamepadAxis::RightY : GamepadAxis::LeftY);
}

// A control contributes only when it is bound, unclaimed, and down.
bool ButtonActive(const InputDeviceSnapshot& devices, const InputControlClaims& claims, InputControl control)
{
    return control.IsValid() && !claims.Has(control) && devices.IsHeld(control);
}

float ButtonAxis(bool negative, bool positive)
{
    return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
}

// Rescale what is past the dead zone back across the full range, so the live
// range still begins at zero instead of stepping to the threshold value.
float ApplyDeadZone(float value, float deadZone)
{
    if (deadZone <= 0.0f)
        return value;
    const float magnitude = std::fabs(value);
    if (magnitude <= deadZone)
        return 0.0f;
    const float rescaled = (magnitude - deadZone) / (1.0f - deadZone);
    return value < 0.0f ? -rescaled : rescaled;
}

void ApplyRadialDeadZone(float& x, float& y, float deadZone)
{
    if (deadZone <= 0.0f)
        return;
    const float magnitude = std::sqrt(x * x + y * y);
    if (magnitude <= deadZone)
    {
        x = 0.0f;
        y = 0.0f;
        return;
    }
    const float rescaled = (magnitude - deadZone) / (1.0f - deadZone);
    const float scale = rescaled / magnitude;
    x *= scale;
    y *= scale;
}

void ClampToUnitCircle(float& x, float& y)
{
    const float magnitudeSquared = x * x + y * y;
    if (magnitudeSquared <= 1.0f)
        return;
    const float scale = 1.0f / std::sqrt(magnitudeSquared);
    x *= scale;
    y *= scale;
}

struct BindingContribution
{
    float X = 0.0f;
    float Y = 0.0f;
    bool Held = false;
    bool Pressed = false;
    bool Released = false;
};

BindingContribution EvaluateBinding(const InputBinding& binding,
                                    const InputDeviceSnapshot& devices,
                                    const InputEdgeLatch& latch,
                                    const InputControlClaims& claims)
{
    BindingContribution result;

    switch (binding.Kind)
    {
    case InputBindingKind::Direct:
    {
        const InputControl control = binding.Controls[kBindingNegativeX];
        if (!control.IsValid() || claims.Has(control))
            return result;

        switch (control.Source)
        {
        case InputControlSource::MouseMotion:
            result.X = latch.MotionX;
            result.Y = latch.MotionY;
            break;
        case InputControlSource::MouseWheel:
            result.X = latch.Wheel;
            break;
        case InputControlSource::GamepadTrigger:
        case InputControlSource::GamepadStick:
            ReadGamepadAxes(devices, control, result.X, result.Y);
            break;
        default:
            result.Held = devices.IsHeld(control);
            result.X = result.Held ? 1.0f : 0.0f;
            break;
        }

        result.Pressed = latch.WasPressed(control);
        result.Released = latch.WasReleased(control);
        break;
    }

    case InputBindingKind::AxisPair:
    {
        const InputControl negative = binding.Controls[kBindingNegativeX];
        const InputControl positive = binding.Controls[kBindingPositiveX];
        result.X = ButtonAxis(ButtonActive(devices, claims, negative),
                              ButtonActive(devices, claims, positive));
        result.Held = result.X != 0.0f;
        result.Pressed = (!claims.Has(negative) && latch.WasPressed(negative))
                      || (!claims.Has(positive) && latch.WasPressed(positive));
        result.Released = (!claims.Has(negative) && latch.WasReleased(negative))
                       || (!claims.Has(positive) && latch.WasReleased(positive));
        break;
    }

    case InputBindingKind::Cardinal:
    {
        const InputControl negativeX = binding.Controls[kBindingNegativeX];
        const InputControl positiveX = binding.Controls[kBindingPositiveX];
        const InputControl negativeY = binding.Controls[kBindingNegativeY];
        const InputControl positiveY = binding.Controls[kBindingPositiveY];
        result.X = ButtonAxis(ButtonActive(devices, claims, negativeX),
                              ButtonActive(devices, claims, positiveX));
        result.Y = ButtonAxis(ButtonActive(devices, claims, negativeY),
                              ButtonActive(devices, claims, positiveY));
        result.Held = result.X != 0.0f || result.Y != 0.0f;

        const InputControl all[kInputBindingControlCount] = { negativeX, positiveX, negativeY, positiveY };
        for (InputControl control : all)
        {
            if (claims.Has(control))
                continue;
            result.Pressed = result.Pressed || latch.WasPressed(control);
            result.Released = result.Released || latch.WasReleased(control);
        }
        break;
    }
    }

    const InputControlSource directSource = binding.Controls[kBindingNegativeX].Source;
    const bool isPlanar = binding.Kind == InputBindingKind::Cardinal
        || (binding.Kind == InputBindingKind::Direct
            && ValueKindOf(directSource) == InputControlValueKind::Axis2D);

    // Only bounded controls clamp. A button composite and a stick both describe
    // a position inside a unit circle, so a diagonal must not out-run a
    // cardinal; pointer displacement is a measurement, and a fast flick
    // legitimately exceeds one unit.
    const bool isBounded = binding.Kind == InputBindingKind::Cardinal
        || directSource == InputControlSource::GamepadStick;
    if (binding.Normalize && isBounded)
        ClampToUnitCircle(result.X, result.Y);

    if (isPlanar)
        ApplyRadialDeadZone(result.X, result.Y, binding.DeadZone);
    else
        result.X = ApplyDeadZone(result.X, binding.DeadZone);

    result.X *= binding.Scale;
    result.Y *= binding.Scale;
    if (binding.InvertX)
        result.X = -result.X;
    if (binding.InvertY)
        result.Y = -result.Y;

    return result;
}

void ClaimBindingControls(const InputBinding& binding, InputControlClaims& claims)
{
    const std::size_t used = binding.Kind == InputBindingKind::Direct     ? 1
                           : binding.Kind == InputBindingKind::AxisPair   ? 2
                                                                         : kInputBindingControlCount;
    for (std::size_t i = 0; i < used; ++i)
    {
        if (binding.Controls[i].IsValid())
            claims.Add(binding.Controls[i]);
    }
}
}

void InputEdgeLatch::Accumulate(const InputFrame& frame)
{
    for (std::uint32_t scancode : frame.KeysPressed)
    {
        if (scancode < kInputScancodeCount)
            KeysPressed[scancode / 64] |= std::uint64_t{ 1 } << (scancode % 64);
    }
    for (std::uint32_t scancode : frame.KeysReleased)
    {
        if (scancode < kInputScancodeCount)
            KeysReleased[scancode / 64] |= std::uint64_t{ 1 } << (scancode % 64);
    }
    for (std::uint32_t button : frame.MouseButtonsPressed)
    {
        if (button < 64)
            MouseButtonsPressed |= std::uint64_t{ 1 } << button;
    }
    for (std::uint32_t button : frame.MouseButtonsReleased)
    {
        if (button < 64)
            MouseButtonsReleased |= std::uint64_t{ 1 } << button;
    }
    for (std::uint32_t button : frame.GamepadButtonsPressed)
    {
        if (button < 32)
            GamepadButtonsPressed |= std::uint32_t{ 1 } << button;
    }
    for (std::uint32_t button : frame.GamepadButtonsReleased)
    {
        if (button < 32)
            GamepadButtonsReleased |= std::uint32_t{ 1 } << button;
    }

    MotionX += frame.MouseDeltaX;
    MotionY += frame.MouseDeltaY;
    Wheel += frame.MouseWheelY;
}

void InputEdgeLatch::Clear()
{
    KeysPressed.fill(0);
    KeysReleased.fill(0);
    MouseButtonsPressed = 0;
    MouseButtonsReleased = 0;
    GamepadButtonsPressed = 0;
    GamepadButtonsReleased = 0;
    MotionX = 0.0f;
    MotionY = 0.0f;
    Wheel = 0.0f;
}

bool InputEdgeLatch::WasPressed(InputControl control) const
{
    switch (control.Source)
    {
    case InputControlSource::Key:
        return control.Index < kInputScancodeCount
            && (KeysPressed[control.Index / 64] & (std::uint64_t{ 1 } << (control.Index % 64))) != 0;
    case InputControlSource::MouseButton:
        return control.Index < 64
            && (MouseButtonsPressed & (std::uint64_t{ 1 } << control.Index)) != 0;
    case InputControlSource::GamepadButton:
        return control.Index < 32
            && (GamepadButtonsPressed & (std::uint32_t{ 1 } << control.Index)) != 0;
    default:
        return false;
    }
}

bool InputEdgeLatch::WasReleased(InputControl control) const
{
    switch (control.Source)
    {
    case InputControlSource::Key:
        return control.Index < kInputScancodeCount
            && (KeysReleased[control.Index / 64] & (std::uint64_t{ 1 } << (control.Index % 64))) != 0;
    case InputControlSource::MouseButton:
        return control.Index < 64
            && (MouseButtonsReleased & (std::uint64_t{ 1 } << control.Index)) != 0;
    case InputControlSource::GamepadButton:
        return control.Index < 32
            && (GamepadButtonsReleased & (std::uint32_t{ 1 } << control.Index)) != 0;
    default:
        return false;
    }
}

void InputDeviceSnapshot::CaptureFrom(const InputFrame& frame)
{
    KeysHeld = frame.KeyHeld;
    MouseButtonsHeld = frame.MouseHeld[0];
    GamepadButtonsHeld = frame.GamepadButtonsHeld;
    GamepadAxes = frame.GamepadAxes;
}

bool InputDeviceSnapshot::IsHeld(InputControl control) const
{
    switch (control.Source)
    {
    case InputControlSource::Key:
        return control.Index < kInputScancodeCount
            && (KeysHeld[control.Index / 64] & (std::uint64_t{ 1 } << (control.Index % 64))) != 0;
    case InputControlSource::MouseButton:
        return control.Index < 64
            && (MouseButtonsHeld & (std::uint64_t{ 1 } << control.Index)) != 0;
    case InputControlSource::GamepadButton:
        return control.Index < 32
            && (GamepadButtonsHeld & (std::uint32_t{ 1 } << control.Index)) != 0;
    default:
        return false;
    }
}

float InputDeviceSnapshot::Axis(GamepadAxis axis) const
{
    const auto index = static_cast<std::size_t>(axis);
    return index < GamepadAxes.size() ? GamepadAxes[index] : 0.0f;
}

void AccumulateInputFrame(InputFrame& frame,
                          InputDeviceSnapshot& devices,
                          InputClockState& presentationClock,
                          InputClockState& simulationClock)
{
    devices.CaptureFrom(frame);
    presentationClock.Latch.Accumulate(frame);
    simulationClock.Latch.Accumulate(frame);

    // The latches now own these transitions. Leaving them on the frame would
    // hand the presentation clock the same press again next frame, since the
    // frame only drops them when a fixed tick runs.
    frame.ClearEdges();
}

void ResolveInputActions(const BoundInputProfile& profile,
                         std::span<const std::uint8_t> contextActive,
                         const InputDeviceSnapshot& devices,
                         InputClockState& clock,
                         std::span<InputActionValue> out)
{
    const std::size_t actionCount = profile.ActionTypes.size();
    if (out.size() < actionCount)
        return;

    for (std::size_t i = 0; i < actionCount; ++i)
        out[i] = InputActionValue{};
    if (clock.HeldPrevious.size() != actionCount)
        clock.HeldPrevious.assign(actionCount, 0);

    InputControlClaims claims;
    for (std::size_t contextIndex = 0; contextIndex < profile.Contexts.size(); ++contextIndex)
    {
        if (contextIndex >= contextActive.size() || contextActive[contextIndex] == 0)
            continue;

        const InputContextDefinition& context = profile.Contexts[contextIndex];
        const std::uint32_t end = context.FirstBinding + context.BindingCount;
        for (std::uint32_t i = context.FirstBinding; i < end && i < profile.Bindings.size(); ++i)
        {
            const InputBinding& binding = profile.Bindings[i];
            if (binding.ActionIndex >= actionCount)
                continue;

            const BindingContribution contribution =
                EvaluateBinding(binding, devices, clock.Latch, claims);
            InputActionValue& value = out[binding.ActionIndex];

            if (profile.ActionTypes[binding.ActionIndex] != InputActionType::Digital)
            {
                value.X += contribution.X;
                value.Y += contribution.Y;
            }
            if (contribution.Held)
                value.Flags |= InputActionFlags::Held;
            if (contribution.Pressed)
                value.Flags |= InputActionFlags::Pressed;
            if (contribution.Released)
                value.Flags |= InputActionFlags::Released;
        }

        // Claiming after the whole context, not per binding, so two bindings in
        // one context can share a control.
        for (std::uint32_t i = context.FirstBinding; i < end && i < profile.Bindings.size(); ++i)
            ClaimBindingControls(profile.Bindings[i], claims);
    }

    for (std::size_t i = 0; i < actionCount; ++i)
    {
        InputActionValue& value = out[i];
        const bool held = value.IsHeld();

        // A press only ever comes from a device transition. Activating a
        // context over an already-held key must not look like a new press, or
        // opening a menu would fire whatever the player happened to be holding.
        // A release also comes from losing the binding, so an action cannot
        // stay held after the context that drove it goes away.
        if (!held && clock.HeldPrevious[i] != 0)
            value.Flags |= InputActionFlags::Released;

        if (profile.ActionTypes[i] == InputActionType::Digital)
            value.X = held ? 1.0f : 0.0f;

        clock.HeldPrevious[i] = held ? 1 : 0;
    }

    clock.Latch.Clear();
}
