#include <input/InputActionResolve.h>

#include <algorithm>
#include <cmath>

namespace
{
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

// Whether this binding turns an analog control into a button, which is the one
// case with no device transition behind it.
bool IsAnalogButton(const InputBinding& binding, InputActionType actionType)
{
    return binding.Kind == InputBindingKind::Direct
        && actionType == InputActionType::Digital
        && binding.Controls[kBindingNegativeX].Source == InputControlSource::GamepadTrigger;
}

bool IsButtonSource(InputControlSource source)
{
    return source == InputControlSource::Key || source == InputControlSource::MouseButton
        || source == InputControlSource::GamepadButton;
}

// The one rule for whether a binding holds its action, used by attribution and
// by resolution alike so the edges and the held flag cannot disagree.
bool BindingHeld(const InputBinding& binding,
                 bool analogButton,
                 bool analogAbove,
                 const InputDeviceSnapshot& devices,
                 const InputControlClaims& claims)
{
    switch (binding.Kind)
    {
    case InputBindingKind::Direct:
    {
        const InputControl control = binding.Controls[kBindingNegativeX];
        if (!control.IsValid() || claims.Has(control))
            return false;
        if (analogButton)
            return analogAbove;
        return IsButtonSource(control.Source) && devices.IsHeld(control);
    }
    case InputBindingKind::AxisPair:
        return ButtonAxis(ButtonActive(devices, claims, binding.Controls[kBindingNegativeX]),
                          ButtonActive(devices, claims, binding.Controls[kBindingPositiveX])) != 0.0f;
    case InputBindingKind::Cardinal:
        return ButtonAxis(ButtonActive(devices, claims, binding.Controls[kBindingNegativeX]),
                          ButtonActive(devices, claims, binding.Controls[kBindingPositiveX])) != 0.0f
            || ButtonAxis(ButtonActive(devices, claims, binding.Controls[kBindingNegativeY]),
                          ButtonActive(devices, claims, binding.Controls[kBindingPositiveY])) != 0.0f;
    }
    return false;
}

struct BindingContribution
{
    float X = 0.0f;
    float Y = 0.0f;
    // True when X/Y came from a control that reports a held position rather
    // than an accumulated displacement. The value is the same either way; what
    // differs is what a time-integrating consumer must do with it.
    bool Sampled = false;
    bool Held = false;
};

BindingContribution EvaluateBinding(const InputBinding& binding,
                                    const InputDeviceSnapshot& devices,
                                    const InputMotionDelta& motion,
                                    const InputControlClaims& claims,
                                    bool analogButton,
                                    bool analogAbove)
{
    BindingContribution result;
    result.Held = BindingHeld(binding, analogButton, analogAbove, devices, claims);

    switch (binding.Kind)
    {
    case InputBindingKind::Direct:
    {
        const InputControl control = binding.Controls[kBindingNegativeX];
        if (!control.IsValid() || claims.Has(control))
            return result;

        // A thresholded control is already a button by the time it gets here;
        // its value is the crossing, so none of the analog conditioning below
        // applies to it.
        if (analogButton)
        {
            result.X = result.Held ? 1.0f : 0.0f;
            return result;
        }

        switch (control.Source)
        {
        case InputControlSource::MouseMotion:
            result.X = motion.X;
            result.Y = motion.Y;
            break;
        case InputControlSource::MouseWheel:
            result.X = motion.Wheel;
            break;
        case InputControlSource::GamepadTrigger:
        case InputControlSource::GamepadStick:
            ReadGamepadAxes(devices, control, result.X, result.Y);
            result.Sampled = true;
            break;
        default:
            result.X = result.Held ? 1.0f : 0.0f;
            result.Sampled = true;
            break;
        }
        break;
    }

    case InputBindingKind::AxisPair:
        result.X = ButtonAxis(ButtonActive(devices, claims, binding.Controls[kBindingNegativeX]),
                              ButtonActive(devices, claims, binding.Controls[kBindingPositiveX]));
        result.Sampled = true;
        break;

    case InputBindingKind::Cardinal:
        result.X = ButtonAxis(ButtonActive(devices, claims, binding.Controls[kBindingNegativeX]),
                              ButtonActive(devices, claims, binding.Controls[kBindingPositiveX]));
        result.Y = ButtonAxis(ButtonActive(devices, claims, binding.Controls[kBindingNegativeY]),
                              ButtonActive(devices, claims, binding.Controls[kBindingPositiveY]));
        result.Sampled = true;
        break;
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

bool ContextIsActive(std::span<const std::uint8_t> contextActive, std::size_t context)
{
    return context < contextActive.size() && contextActive[context] != 0;
}

// The claims each context sees: everything the active contexts above it bind.
// Claiming after the whole context, not per binding, so two bindings in one
// context can share a control.
void BuildContextClaims(const BoundInputProfile& profile,
                        std::span<const std::uint8_t> contextActive,
                        std::vector<InputControlClaims>& claimsByContext)
{
    claimsByContext.resize(profile.Contexts.size());
    InputControlClaims claims;
    for (std::size_t contextIndex = 0; contextIndex < profile.Contexts.size(); ++contextIndex)
    {
        claimsByContext[contextIndex] = claims;
        if (!ContextIsActive(contextActive, contextIndex))
            continue;
        const InputContextDefinition& context = profile.Contexts[contextIndex];
        const std::uint32_t end = context.FirstBinding + context.BindingCount;
        for (std::uint32_t i = context.FirstBinding; i < end && i < profile.Bindings.size(); ++i)
            ClaimBindingControls(profile.Bindings[i], claims);
    }
}

// Which actions the attributed devices hold, into `held`.
void EvaluateActionsHeld(const BoundInputProfile& profile,
                         std::span<const std::uint8_t> contextActive,
                         const InputEdgeAttribution& attribution,
                         std::vector<std::uint8_t>& held)
{
    const std::size_t actionCount = profile.ActionTypes.size();
    held.assign(actionCount, 0);
    for (std::size_t contextIndex = 0; contextIndex < profile.Contexts.size(); ++contextIndex)
    {
        if (!ContextIsActive(contextActive, contextIndex))
            continue;
        const InputContextDefinition& context = profile.Contexts[contextIndex];
        const InputControlClaims& claims = attribution.ContextClaims[contextIndex];
        const std::uint32_t end = context.FirstBinding + context.BindingCount;
        for (std::uint32_t i = context.FirstBinding; i < end && i < profile.Bindings.size(); ++i)
        {
            const InputBinding& binding = profile.Bindings[i];
            if (binding.ActionIndex >= actionCount || held[binding.ActionIndex] != 0)
                continue;
            const bool analogButton = IsAnalogButton(binding, profile.ActionTypes[binding.ActionIndex]);
            if (BindingHeld(binding, analogButton, attribution.AnalogAbove[i] != 0, attribution.Devices, claims))
                held[binding.ActionIndex] = 1;
        }
    }
}

// How a change in what is held reaches the edges: a device transition is the
// player; anything else that moves an action is not.
enum class HeldChangeCause : std::uint8_t
{
    Player,
    Interruption,
};

void AppendEdge(InputClockState& presentation, InputClockState& simulation, std::size_t action, InputEdgeKind kind)
{
    if (action > 0xFF)
        return;
    const InputActionEdge edge{ static_cast<std::uint8_t>(action), kind };
    presentation.Pending.Append(edge);
    simulation.Pending.Append(edge);
}

// Compares the actions now held with the ones attributed so far and publishes
// the difference. A rise the player did not cause is no press: activating a
// context over a held key must not fire what the player happened to be holding.
void PublishHeldChanges(InputEdgeAttribution& attribution,
                        const std::vector<std::uint8_t>& held,
                        HeldChangeCause cause,
                        InputClockState& presentation,
                        InputClockState& simulation)
{
    for (std::size_t action = 0; action < held.size(); ++action)
    {
        const bool was = attribution.ActionHeld[action] != 0;
        const bool now = held[action] != 0;
        if (was == now)
            continue;
        attribution.ActionHeld[action] = now ? 1 : 0;
        if (now && cause == HeldChangeCause::Player)
            AppendEdge(presentation, simulation, action, InputEdgeKind::Press);
        else if (!now)
            AppendEdge(presentation, simulation, action,
                       cause == HeldChangeCause::Player ? InputEdgeKind::Release
                                                        : InputEdgeKind::InterruptedRelease);
    }
}

void UpdateAnalogThresholds(const BoundInputProfile& profile, InputEdgeAttribution& attribution)
{
    attribution.AnalogAbove.resize(profile.Bindings.size(), 0);
    for (std::size_t i = 0; i < profile.Bindings.size(); ++i)
    {
        const InputBinding& binding = profile.Bindings[i];
        if (binding.ActionIndex >= profile.ActionTypes.size()
            || !IsAnalogButton(binding, profile.ActionTypes[binding.ActionIndex]))
        {
            attribution.AnalogAbove[i] = 0;
            continue;
        }
        float value = 0.0f;
        float unused = 0.0f;
        ReadGamepadAxes(attribution.Devices, binding.Controls[kBindingNegativeX], value, unused);
        attribution.AnalogAbove[i] = value >= binding.Threshold ? 1 : 0;
    }
}

// Walks a pass's edges from the held state the previous pass left, turning them
// into the summary flags; then whatever the walk leaves held that the devices do
// not is owed an interrupted fall. Edges never invent a press.
void SummarizeEdges(std::span<InputActionValue> out,
                    std::span<const InputActionFireMode> fireModes,
                    std::span<const InputActionType> types,
                    std::vector<std::uint8_t>& walked,
                    InputActionEdgeList& edges)
{
    for (const InputActionEdge& edge : edges.View())
    {
        if (edge.Action >= out.size())
            continue;
        InputActionValue& value = out[edge.Action];
        const bool digital = edge.Action < types.size() && types[edge.Action] == InputActionType::Digital;
        const InputActionFireMode mode =
            edge.Action < fireModes.size() ? fireModes[edge.Action] : InputActionFireMode::Held;
        switch (edge.Kind)
        {
        case InputEdgeKind::Press:
            value.Flags |= InputActionFlags::Pressed;
            if (digital && mode == InputActionFireMode::Pressed)
                value.Flags |= InputActionFlags::Fired;
            walked[edge.Action] = 1;
            break;
        case InputEdgeKind::Release:
            value.Flags |= InputActionFlags::Released;
            if (digital && mode == InputActionFireMode::Released)
                value.Flags |= InputActionFlags::Fired;
            walked[edge.Action] = 0;
            break;
        case InputEdgeKind::InterruptedRelease:
            value.Flags |= InputActionFlags::Released | InputActionFlags::Interrupted;
            walked[edge.Action] = 0;
            break;
        }
    }

    for (std::size_t action = 0; action < out.size(); ++action)
    {
        if (walked[action] == 0 || out[action].IsHeld())
            continue;
        out[action].Flags |= InputActionFlags::Released | InputActionFlags::Interrupted;
        edges.Append({ static_cast<std::uint8_t>(action), InputEdgeKind::InterruptedRelease });
    }
}
} // namespace

void InputControlClaims::Add(InputControl control)
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

bool InputControlClaims::Has(InputControl control) const
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

void InputMotionLatch::Accumulate(const InputFrame& frame)
{
    X += frame.MouseDeltaX;
    Y += frame.MouseDeltaY;
    Wheel += frame.MouseWheelY;
}

void InputMotionLatch::Clear()
{
    X = 0.0f;
    Y = 0.0f;
    Wheel = 0.0f;
}

InputMotionDelta InputMotionLatch::Share(std::uint32_t shares) const
{
    if (shares <= 1)
        return InputMotionDelta{ .X = X, .Y = Y, .Wheel = Wheel };

    const float scale = 1.0f / static_cast<float>(shares);
    return InputMotionDelta{ .X = X * scale, .Y = Y * scale, .Wheel = Wheel * scale };
}

void InputMotionLatch::Consume(const InputMotionDelta& taken)
{
    X -= taken.X;
    Y -= taken.Y;
    Wheel -= taken.Wheel;
}

void InputDeviceSnapshot::CaptureFrom(const InputFrame& frame)
{
    CaptureButtonsFrom(frame);
    GamepadAxes = frame.GamepadAxes;
}

void InputDeviceSnapshot::CaptureButtonsFrom(const InputFrame& frame)
{
    KeysHeld = frame.KeyHeld;
    MouseButtonsHeld = frame.MouseHeld[0];
    GamepadButtonsHeld = frame.GamepadButtonsHeld;
}

void InputDeviceSnapshot::Apply(const InputTransition& transition)
{
    const InputControl control = transition.Control;
    switch (control.Source)
    {
    case InputControlSource::Key:
        if (control.Index < kInputScancodeCount)
        {
            const std::uint64_t bit = std::uint64_t{ 1 } << (control.Index % 64);
            std::uint64_t& word = KeysHeld[control.Index / 64];
            word = transition.Down ? (word | bit) : (word & ~bit);
        }
        break;
    case InputControlSource::MouseButton:
        if (control.Index < 64)
        {
            const std::uint64_t bit = std::uint64_t{ 1 } << control.Index;
            MouseButtonsHeld = transition.Down ? (MouseButtonsHeld | bit) : (MouseButtonsHeld & ~bit);
        }
        break;
    case InputControlSource::GamepadButton:
        if (control.Index < 32)
        {
            const std::uint32_t bit = std::uint32_t{ 1 } << control.Index;
            GamepadButtonsHeld = transition.Down ? (GamepadButtonsHeld | bit) : (GamepadButtonsHeld & ~bit);
        }
        break;
    default:
        break;
    }
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

void AttributeInputFrame(InputFrame& frame,
                         const BoundInputProfile* profile,
                         std::span<const std::uint8_t> contextActive,
                         InputEdgeAttribution& attribution,
                         InputClockState& presentationClock,
                         InputClockState& simulationClock)
{
    presentationClock.Motion.Accumulate(frame);
    simulationClock.Motion.Accumulate(frame);

    if (profile == nullptr)
    {
        for (std::size_t action = 0; action < attribution.ActionHeld.size(); ++action)
            if (attribution.ActionHeld[action] != 0)
                AppendEdge(presentationClock, simulationClock, action, InputEdgeKind::InterruptedRelease);
        attribution.ActionHeld.clear();
        attribution.AnalogAbove.clear();
        attribution.Devices.CaptureFrom(frame);
        frame.ClearTransitions();
        return;
    }

    attribution.ActionHeld.resize(profile->ActionTypes.size(), 0);
    attribution.AnalogAbove.resize(profile->Bindings.size(), 0);
    BuildContextClaims(*profile, contextActive, attribution.ContextClaims);
    std::vector<std::uint8_t>& held = attribution.Scratch;

    // In order: what this frame's contexts did to what was already held, then
    // each transition as it arrived, then the triggers' resting positions, then
    // whatever the log could not carry, against the frame's real buttons.
    EvaluateActionsHeld(*profile, contextActive, attribution, held);
    PublishHeldChanges(attribution, held, HeldChangeCause::Interruption, presentationClock, simulationClock);

    for (const InputTransition& transition : frame.TransitionLog())
    {
        attribution.Devices.Apply(transition);
        EvaluateActionsHeld(*profile, contextActive, attribution, held);
        PublishHeldChanges(attribution, held,
                           transition.Synthetic ? HeldChangeCause::Interruption : HeldChangeCause::Player,
                           presentationClock, simulationClock);
    }

    attribution.Devices.GamepadAxes = frame.GamepadAxes;
    UpdateAnalogThresholds(*profile, attribution);
    EvaluateActionsHeld(*profile, contextActive, attribution, held);
    PublishHeldChanges(attribution, held, HeldChangeCause::Player, presentationClock, simulationClock);

    attribution.Devices.CaptureButtonsFrom(frame);
    EvaluateActionsHeld(*profile, contextActive, attribution, held);
    PublishHeldChanges(attribution, held, HeldChangeCause::Interruption, presentationClock, simulationClock);

    frame.ClearTransitions();
}

void ResolveInputActions(const BoundInputProfile& profile,
                         std::span<const std::uint8_t> contextActive,
                         const InputEdgeAttribution& attribution,
                         InputClockState& clock,
                         std::span<InputActionValue> out,
                         InputActionEdgeList& outEdges,
                         std::uint32_t displacementShares,
                         std::span<InputActionValue> outSampled)
{
    const std::size_t actionCount = profile.ActionTypes.size();
    if (out.size() < actionCount)
        return;
    const bool trackSampled = outSampled.size() >= actionCount;

    // Read once, before any binding sees it, so every binding in this pass
    // resolves against the same displacement and the drain below subtracts
    // exactly what was handed out.
    const InputMotionDelta motion = clock.Motion.Share(displacementShares);

    for (std::size_t i = 0; i < actionCount; ++i)
        out[i] = InputActionValue{};
    if (trackSampled)
        for (std::size_t i = 0; i < actionCount; ++i)
            outSampled[i] = InputActionValue{};
    clock.HeldPrevious.resize(actionCount, 0);

    for (std::size_t contextIndex = 0; contextIndex < profile.Contexts.size(); ++contextIndex)
    {
        if (!ContextIsActive(contextActive, contextIndex) || contextIndex >= attribution.ContextClaims.size())
            continue;

        const InputContextDefinition& context = profile.Contexts[contextIndex];
        const InputControlClaims& claims = attribution.ContextClaims[contextIndex];
        const std::uint32_t end = context.FirstBinding + context.BindingCount;
        for (std::uint32_t i = context.FirstBinding; i < end && i < profile.Bindings.size(); ++i)
        {
            const InputBinding& binding = profile.Bindings[i];
            if (binding.ActionIndex >= actionCount)
                continue;

            const bool analogButton = IsAnalogButton(binding, profile.ActionTypes[binding.ActionIndex]);
            const bool analogAbove = i < attribution.AnalogAbove.size() && attribution.AnalogAbove[i] != 0;
            const BindingContribution contribution =
                EvaluateBinding(binding, attribution.Devices, motion, claims, analogButton, analogAbove);
            InputActionValue& value = out[binding.ActionIndex];

            if (profile.ActionTypes[binding.ActionIndex] != InputActionType::Digital)
            {
                value.X += contribution.X;
                value.Y += contribution.Y;
                if (trackSampled && contribution.Sampled)
                {
                    outSampled[binding.ActionIndex].X += contribution.X;
                    outSampled[binding.ActionIndex].Y += contribution.Y;
                }
            }
            if (contribution.Held)
                value.Flags |= InputActionFlags::Held;
        }
    }

    // An axis action carries a magnitude rather than a moment, so it never fires.
    for (std::size_t i = 0; i < actionCount; ++i)
    {
        InputActionValue& value = out[i];
        if (profile.ActionTypes[i] != InputActionType::Digital)
            continue;
        value.X = value.IsHeld() ? 1.0f : 0.0f;
        if (value.IsHeld() && profile.ActionFireModes[i] == InputActionFireMode::Held)
            value.Flags |= InputActionFlags::Fired;
    }

    outEdges = clock.Pending;
    clock.Pending.Clear();
    SummarizeEdges(out.first(actionCount), profile.ActionFireModes, profile.ActionTypes, clock.HeldPrevious, outEdges);
    for (std::size_t i = 0; i < actionCount; ++i)
        clock.HeldPrevious[i] = out[i].IsHeld() ? 1 : 0;

    clock.Motion.Consume(motion);
}

void ReleaseInputActions(InputClockState& clock,
                         std::span<InputActionValue> out,
                         InputActionEdgeList& outEdges)
{
    for (InputActionValue& value : out)
        value = InputActionValue{};
    clock.HeldPrevious.resize(out.size(), 0);

    // The profile that knew each action's fire mode is gone, so nothing fires.
    outEdges = clock.Pending;
    clock.Pending.Clear();
    SummarizeEdges(out, {}, {}, clock.HeldPrevious, outEdges);
    std::fill(clock.HeldPrevious.begin(), clock.HeldPrevious.end(), std::uint8_t{ 0 });
}

void DiscardSuspendedInput(InputClockState& clock)
{
    clock.Motion.Clear();
    const InputActionEdgeList pending = clock.Pending;
    clock.Pending.Clear();
    for (const InputActionEdge& edge : pending.View())
        if (edge.Kind == InputEdgeKind::InterruptedRelease)
            clock.Pending.Append(edge);
}
