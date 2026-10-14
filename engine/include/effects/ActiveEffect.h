#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <limits>
#include <type_traits>

inline constexpr std::uint8_t kEffectGrantedTagCapacity = 8;
inline constexpr std::uint32_t kEffectNeverEnds = std::numeric_limits<std::uint32_t>::max();

// One applied Duration/Infinite effect, on its own entity in the persistent
// partition. Its timing and lifetime rules: docs/gameplay/abilitykit.md, "Effects".
struct SENCHA_COMPONENT("sencha.active_effect") ActiveEffect
{
    EntityId Target;
    EffectId Def;
    LeaseOwner Owner;
    float Scale = 1.0f;
    EffectTimeDomain TimeDomain = EffectTimeDomain::Target;
    // Application order within this World: the fold's tie-break.
    std::uint64_t Sequence = 0;
    std::uint32_t RemainingTicks = kEffectNeverEnds;
    std::uint32_t PeriodTicks = 0;
    std::uint32_t TicksToPeriod = 0;
    // The tags this instance actually granted, so ending it never needs the definition.
    GameplayTagId GrantedTags[kEffectGrantedTagCapacity] = {};
    std::uint8_t GrantedTagCount = 0;
};

static_assert(std::is_trivially_copyable_v<ActiveEffect>,
              "ActiveEffect must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <effects/ActiveEffect.sencha.h>
#endif
