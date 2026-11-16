#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <world/identity/PersistableRef.h>

#include <cstdint>
#include <type_traits>

// docs/gameplay/held-records.md. A record entity owned by a holder entity; an
// invalid holder is the world. Records are ordered under their holder by
// Position, then by when they were listed.
struct SENCHA_COMPONENT("sencha.held")
       SENCHA_SCHEMA("Held")
       SENCHA_SCENE_CHUNK("HELD")
       SENCHA_AUTHORITY_MATERIALIZED
Held
{
    SENCHA_FIELD("holder")
    SENCHA_LABEL("Holder")
    PersistableRef Holder{};

    SENCHA_FIELD("position")
    SENCHA_LABEL("Position")
    std::uint16_t Position = 0;
};

static_assert(std::is_trivially_copyable_v<Held>, "Held lives in ECS chunks");

template <>
struct ComponentTraits<Held>
{
    static void OnAdd(Held& held, World& world, EntityId record);
    static void OnRemove(const Held& held, World& world, EntityId record);
};

#if !defined(SENCHA_CODEGEN)
#  include <world/held/Held.sencha.h>
#  include <world/held/HeldWire.h>
#endif
