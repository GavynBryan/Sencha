#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <type_traits>

class World;

// Advanced by every relocation (docs/gameplay/relocation.md). Only equality
// means anything, so it may wrap.
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
