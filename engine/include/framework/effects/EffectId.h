#pragma once

#include <core/identity/StrongId.h>

#include <cstdint>

// Compact id for an effect definition (GameplayEffect equivalent). Definitions
// live in the EffectRegistry; runtime code references ids. Zero is invalid.
using EffectId = StrongId<struct EffectIdTag, std::uint32_t>;
