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
