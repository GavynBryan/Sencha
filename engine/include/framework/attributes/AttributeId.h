#pragma once

#include <core/identity/StrongId.h>

#include <cstdint>

// Compact id for a gameplay attribute (Health, Stamina, Poise, ...). Names live
// in the AttributeRegistry; runtime systems compare ids. Zero is invalid.
using AttributeId = StrongId<struct AttributeIdTag, std::uint32_t>;
