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

// Present while an actor's scale is not one, so the systems that step undilated
// actors in bulk can leave it to the ones that step dilated actors one at a time.
// Maintained by the actor clock; nothing else adds or removes it.
struct SENCHA_COMPONENT("sencha.actor_time_dilated") ActorTimeDilated
{
};

[[nodiscard]] inline double ActorScaleValue(std::int64_t scaleQ16)
{
    return static_cast<double>(scaleQ16) / static_cast<double>(kActorTimeOne);
}

// Whole actor ticks elapsed since the clock started.
[[nodiscard]] inline std::uint64_t ActorTicks(const ActorTime& time)
{
    return static_cast<std::uint64_t>(time.LocalQ16 >> 16);
}

// Whole actor ticks this fixed tick advanced the clock across: none for a slowed
// actor most ticks, more than one for a hastened one some ticks.
[[nodiscard]] inline std::uint32_t ActorTicksAdvanced(const ActorTime& time)
{
    return static_cast<std::uint32_t>((time.LocalQ16 >> 16) - ((time.LocalQ16 - time.ScaleQ16) >> 16));
}

#if !defined(SENCHA_CODEGEN)
#  include <time/ActorTime.sencha.h>
#endif
