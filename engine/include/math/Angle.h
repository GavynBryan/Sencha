#pragma once

#include <concepts>
#include <numbers>

// Each factor is rounded to T once, so a conversion is a single multiply and
// whole-degree constants match their correctly rounded radian values.
template <std::floating_point T>
[[nodiscard]] constexpr T ToRadians(T degrees) noexcept
{
    return degrees * static_cast<T>(std::numbers::pi / 180.0);
}

template <std::floating_point T>
[[nodiscard]] constexpr T ToDegrees(T radians) noexcept
{
    return radians * static_cast<T>(180.0 / std::numbers::pi);
}
