#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <type_traits>

struct SENCHA_COMPONENT("sencha.counter")
       SENCHA_SCHEMA("Counter")
       SENCHA_SCENE_CHUNK("CNTR")
       SENCHA_REPLICATED
       SENCHA_AUTHORITY_MATERIALIZED
       SENCHA_SAVED
Counter
{
    SENCHA_FIELD("name")
    SENCHA_LABEL("Name")
    SENCHA_TOOLTIP("The gameplay tag the counter is kept and found under.")
    GameplayTagId Name;

    SENCHA_FIELD("value")
    SENCHA_LABEL("Value")
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
#endif
