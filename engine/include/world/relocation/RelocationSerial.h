#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <type_traits>

class World;

// How many times this entity has been relocated (docs/gameplay/relocation.md).
// A consumer that remembers the value it last saw knows the entity was moved
// discontinuously since then when the two differ; only equality means anything,
// so the count may wrap. Added by the first relocation and sent to every machine.
struct SENCHA_COMPONENT("sencha.relocation_serial")
       SENCHA_SCHEMA("RelocationSerial")
       SENCHA_REPLICATED
RelocationSerial
{
    SENCHA_FIELD("value")
    std::uint32_t Value = 0;
};

static_assert(std::is_trivially_copyable_v<RelocationSerial>);

// Zero for an entity never relocated.
[[nodiscard]] std::uint32_t RelocationSerialOf(const World& world, EntityId entity);

#if !defined(SENCHA_CODEGEN)
#  include <world/relocation/RelocationSerial.sencha.h>
#endif
