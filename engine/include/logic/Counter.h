#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <type_traits>

// docs/gameplay/counters.md. One named integer that is not zero, on a record
// entity whose Held names the world or a participant's body.
struct SENCHA_COMPONENT("sencha.counter") SENCHA_AUTHORITY_MATERIALIZED Counter
{
    GameplayTagId Name;
    std::int32_t Value = 0;
};

static_assert(std::is_trivially_copyable_v<Counter>, "Counter lives in ECS chunks");

struct HeldRecordKey;
[[nodiscard]] HeldRecordKey CounterKey(GameplayTagId name);

template <>
struct ComponentTraits<Counter>
{
    static void OnAdd(Counter& counter, World& world, EntityId record);
    static void OnRemove(const Counter& counter, World& world, EntityId record);
};

#if !defined(SENCHA_CODEGEN)
#  include <logic/Counter.sencha.h>
#  include <logic/CounterWire.h>
#endif
