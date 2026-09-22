#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

//=============================================================================
// Animation runtime vocabulary (docs/plans/animation-runtime.md)
//
// The fixed capacities the architecture names, and the small closed
// enumerations every animation asset and component shares. Capacities are
// architecture, not tuning: per-entity state is SoA arrays of known size, so a
// rig that needs more than these is a rig that has to be restructured, not a
// constant that gets raised.
//=============================================================================

// A fixed simulation tick, as FixedSimTime::TickIndex counts them.
using AnimTick = std::uint64_t;

// No tick: a mark that has not been set, a lifetime that never ends.
inline constexpr AnimTick kAnimNoTick = ~AnimTick{ 0 };

inline constexpr std::size_t kAnimMaxLayers = 8;
inline constexpr std::size_t kAnimRequestCapacity = 8;
inline constexpr std::size_t kAnimRequestParams = 4;
inline constexpr std::size_t kAnimFactsSmall = 16;
inline constexpr std::size_t kAnimFactsLarge = 64;

// Most rules with a cooldown one layer's flattened selector may hold. Cooldown
// expiry is per-entity state, and fixed state needs a fixed count.
inline constexpr std::size_t kAnimCooldownSlots = 4;

// No rule is winning; no row or content resolved.
inline constexpr std::uint16_t kAnimNoRule = 0xFFFF;
inline constexpr std::uint16_t kAnimNoContent = 0xFFFF;

// Every temporal derivation's window is bounded by this, enforced when the
// schema compiles. It is the horizon after which every derived fact is exact
// for an entity observed from scratch, which is what late join relies on.
inline constexpr float kAnimMaxDerivationWindowMs = 1000.0f;

enum class AnimFactKind : std::uint8_t
{
    Bool,
    Float,
    Int,
    // One interned gameplay tag.
    Tag,
    // A reference to the entity's counted tag set (GameplayTagContainer). The
    // slot itself holds nothing; tag predicates read the container.
    TagSet,
};

[[nodiscard]] std::string_view AnimFactKindName(AnimFactKind kind);

// How much fact storage a rig's entities carry. Chosen per rig; a Prop rig has
// neither and carries no facts at all.
enum class AnimFactCapacity : std::uint8_t
{
    Small,
    Large,
};

[[nodiscard]] constexpr std::size_t AnimFactSlotCount(AnimFactCapacity capacity)
{
    return capacity == AnimFactCapacity::Small ? kAnimFactsSmall : kAnimFactsLarge;
}

enum class AnimRequestLifetime : std::uint8_t
{
    // Lives until its source cancels it.
    Held,
    // Expires on its own at startTick + ticks.
    Fixed,
    // Observed for exactly the tick it starts on.
    Impulse,
};

enum class AnimCancelReason : std::uint8_t
{
    None,
    Released,
    Interrupted,
    Failed,
    Superseded,
};

[[nodiscard]] std::string_view AnimCancelReasonName(AnimCancelReason reason);
[[nodiscard]] std::string_view AnimRequestLifetimeName(AnimRequestLifetime lifetime);
