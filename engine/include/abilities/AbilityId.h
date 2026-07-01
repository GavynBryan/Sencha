#pragma once

#include <core/identity/StrongId.h>

#include <cstdint>

// Compact id for an ability definition. Definitions live in the AbilityRegistry;
// entities are granted abilities by id via AbilitySet. Zero is invalid.
using AbilityId = StrongId<struct AbilityIdTag, std::uint32_t>;
