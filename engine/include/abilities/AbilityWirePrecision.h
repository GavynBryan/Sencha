#pragma once

#include <net/ReplicationSchemas.h>

#include <cstdint>

// Where a presented hit landed is sent only as precisely as an effect at it can
// show: about three centimetres over the replicated position range, and a normal
// to about a degree. A pulse's hit is most of what a firing actor costs a peer.
inline constexpr std::uint8_t kAbilityHitPointBits = 16;
inline constexpr std::uint8_t kAbilityHitNormalBits = 8;
