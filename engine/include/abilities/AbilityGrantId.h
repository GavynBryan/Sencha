#pragma once

#include <core/identity/StrongId.h>

#include <cstdint>

// One grant on one actor. Minted per actor and never reused, so revoking one grant
// ends exactly the activations it started.
using AbilityGrantId = StrongId<struct AbilityGrantIdTag, std::uint32_t>;
