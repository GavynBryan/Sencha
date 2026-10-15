#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/LeaseOwner.h>
#include <time/ActorTime.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

inline constexpr std::size_t kActorTimeLeaseCapacity = 8;
// Ended leases kept for replay, apart from the active ones, so a burst of short
// leases can never use up the room a new one needs.
inline constexpr std::size_t kActorTimeHistoryCapacity = 16;
// How long an ended lease is kept: the depth of the command history a replay
// re-runs.
inline constexpr std::uint64_t kActorTimeHistoryTicks = 64;
inline constexpr std::uint64_t kActorTimeOpen = std::numeric_limits<std::uint64_t>::max();

// One holder's claim on an actor's time scale, effective over simulation ticks
// [FromTick, UntilTick).
struct ActorTimeLease
{
    LeaseOwner Owner{};
    std::int64_t ScaleQ16 = 0;
    std::uint64_t FromTick = 0;
    std::uint64_t UntilTick = kActorTimeOpen;
};

// Every claim on an actor's time scale that is held, and the ones that ended
// recently enough for a replay to need: the entries are the history, so a replayed
// tick computes the scale it had from them.
struct SENCHA_COMPONENT("sencha.actor_time_leases") ActorTimeLeases
{
    ActorTimeLease Active[kActorTimeLeaseCapacity] = {};
    std::uint8_t ActiveCount = 0;
    ActorTimeLease History[kActorTimeHistoryCapacity] = {};
    std::uint8_t HistoryCount = 0;
    // History entries dropped for room before their window closed.
    std::uint32_t Evicted = 0;
};

static_assert(std::is_trivially_copyable_v<ActorTimeLeases>,
              "ActorTimeLeases must be trivially copyable to live in ECS chunks");

// The bounds every actor's scale is clamped to, in Q16.
struct ActorScaleLimits
{
    std::int64_t MinimumQ16 = (kActorTimeOne + 5) / 10;
    std::int64_t MaximumQ16 = 2 * kActorTimeOne;
};

// The fastest any limit may allow: a hastened character's substeps grow with it.
inline constexpr std::int64_t kActorScaleHardMaximumQ16 = 4 * kActorTimeOne;

// Acquired during `tick`, a lease takes effect from the next. False, and nothing
// held, when every active slot is in use.
bool AcquireActorTimeLease(ActorTimeLeases& leases, const LeaseOwner& owner, std::int64_t scaleQ16,
                           std::uint64_t tick);
// Released during `tick`, a lease holds through it. Every lease `owner` holds ends.
void ReleaseActorTimeLeases(ActorTimeLeases& leases, const LeaseOwner& owner, std::uint64_t tick);
// Drops history whose window has closed by `tick`.
void PruneActorTimeHistory(ActorTimeLeases& leases, std::uint64_t tick);

// The scale in force on `tick`: the product of every lease effective on it, taken in
// ascending order of scale so every machine rounds the same way whoever holds what,
// with Q16 rounding at each step, clamped to `limits`.
[[nodiscard]] std::int64_t ActorScaleAt(const ActorTimeLeases& leases, std::uint64_t tick,
                                        const ActorScaleLimits& limits);

#if !defined(SENCHA_CODEGEN)
#  include <time/ActorTimeLeases.sencha.h>
#  include <time/ActorTimeLeasesWire.h>
#endif
