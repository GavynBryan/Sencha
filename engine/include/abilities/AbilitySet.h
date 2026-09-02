#pragma once

#include <ecs/ComponentAnnotations.h>
#include <abilities/AbilityId.h>

#include <cstdint>
#include <type_traits>

//=============================================================================
// AbilitySet
//
// The abilities an entity has been granted, as a trivially-copyable ECS
// component. Small fixed capacity, linear lookup. Activation references an
// ability the entity holds; granting/revoking is a gameplay action.
//=============================================================================
struct SENCHA_COMPONENT("sencha.ability_set") AbilitySet
{
    static constexpr std::uint8_t Capacity = 16;

    AbilityId Abilities[Capacity];
    std::uint8_t Count = 0;

    bool Grant(AbilityId id);  // false if already present, invalid, or full
    bool Revoke(AbilityId id); // false if absent
    [[nodiscard]] bool Has(AbilityId id) const;

    [[nodiscard]] std::uint8_t Size() const { return Count; }
    [[nodiscard]] bool Empty() const { return Count == 0; }
};

static_assert(std::is_trivially_copyable_v<AbilitySet>,
              "AbilitySet must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <abilities/AbilitySet.sencha.h>
#endif
