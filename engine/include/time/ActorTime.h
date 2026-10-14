#pragma once

#include <ecs/ComponentAnnotations.h>

#include <cstdint>
#include <type_traits>

inline constexpr std::int64_t kActorTimeOne = std::int64_t{ 1 } << 16;

// One actor's local clock, in Q16 ticks: it advances ScaleQ16 per fixed tick, so
// timers measured on it run at the actor's own rate. Integer, so every machine
// that applies the same scales reaches the same time.
struct SENCHA_COMPONENT("sencha.actor_time") ActorTime
{
    std::int64_t ScaleQ16 = kActorTimeOne;
    std::int64_t LocalQ16 = 0;
};

static_assert(std::is_trivially_copyable_v<ActorTime>,
              "ActorTime must be trivially copyable to live in ECS chunks");

// Whole actor ticks elapsed since the clock started.
[[nodiscard]] inline std::uint64_t ActorTicks(const ActorTime& time)
{
    return static_cast<std::uint64_t>(time.LocalQ16 >> 16);
}

#if !defined(SENCHA_CODEGEN)
#  include <time/ActorTime.sencha.h>
#endif
