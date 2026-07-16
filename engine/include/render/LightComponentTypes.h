#pragma once

#include <cstdint>

enum class ShadowResolutionTier : std::uint16_t
{
    Low = 256,
    Medium = 512,
    High = 1024,
};

enum class ShadowUpdatePolicy : std::uint8_t
{
    EveryFrame,
    OnChange,
    Static,
};

enum class LightBakeContribution : std::uint8_t
{
    None,
    Indirect,
};
