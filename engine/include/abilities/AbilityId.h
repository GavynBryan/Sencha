#pragma once

#include <core/identity/StrongId.h>

#include <cstdint>

// Compact id for an ability in one World's AbilityRegistry. Zero is invalid.
using AbilityId = StrongId<struct AbilityIdTag, std::uint32_t>;
