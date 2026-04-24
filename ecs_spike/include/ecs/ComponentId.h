#pragma once

#include <cstdint>
#include <cstddef>

// ComponentId: a stable small-integer assigned at registration time.
// uint16_t is wide enough for the 256-component v1 budget with headroom.
using ComponentId = uint16_t;
constexpr ComponentId InvalidComponentId = UINT16_MAX;

// Maximum registered components per world in v1.
// This is the enforced signature bitset width; ComponentId type width exceeds it
// deliberately so the type can later expand without signature format changes.
constexpr size_t MaxComponents = 256;
