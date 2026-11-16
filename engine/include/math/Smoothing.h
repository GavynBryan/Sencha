#pragma once

#include <cmath>

// The share of what remains that an exponential approach with time constant
// `timeConstant` covers in `seconds`: none when no time passes, all of it when
// the constant is zero. The same over one long step as over many short ones.
[[nodiscard]] inline double ExponentialApproachFactor(double seconds, double timeConstant)
{
    if (!(seconds > 0.0))
        return 0.0;
    if (!(timeConstant > 0.0))
        return 1.0;
    return 1.0 - std::exp(-seconds / timeConstant);
}

[[nodiscard]] inline float ApproachExponentially(float current, float target, double seconds, double timeConstant)
{
    return current + (target - current) * static_cast<float>(ExponentialApproachFactor(seconds, timeConstant));
}

// Moves `value` toward `target` over `seconds` as a damped spring of natural
// `frequency` hertz: `damping` 1 arrives without overshoot, less swings past and
// settles back. A spring with no frequency is rigid and is at its target at once.
inline void StepSpring(float& value, float& velocity, float target, double seconds, float frequency, float damping)
{
    if (!(frequency > 0.0f))
    {
        value = target;
        velocity = 0.0f;
        return;
    }
    if (!(seconds > 0.0))
        return;
    // Substeps keep each one well inside where semi-implicit integration is stable.
    constexpr double kMaxPhasePerStep = 0.25;
    const double omega = 2.0 * 3.14159265358979323846 * static_cast<double>(frequency);
    const int steps = static_cast<int>(std::ceil(seconds * omega / kMaxPhasePerStep));
    const double step = seconds / static_cast<double>(steps > 0 ? steps : 1);
    double x = value;
    double v = velocity;
    for (int i = 0; i < steps; ++i)
    {
        v += (omega * omega * (static_cast<double>(target) - x) - 2.0 * static_cast<double>(damping) * omega * v) * step;
        x += v * step;
    }
    value = static_cast<float>(x);
    velocity = static_cast<float>(v);
}
