#pragma once

#include <abilities/AbilityCapacity.h>
#include <abilities/AbilityCueLog.h>
#include <abilities/AbilityGrantId.h>
#include <abilities/AbilityId.h>
#include <abilities/AbilityLocals.h>
#include <abilities/AbilityPresentationState.h>
#include <abilities/ActiveAbilities.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <time/ActorTime.h>

#include <cstdint>
#include <tuple>
#include <type_traits>

enum class AbilityGrantKind : std::uint8_t
{
    // A grant entry authored on the actor.
    Authored,
    // Held by a lasting effect for as long as it lasts.
    Effect,
    // Granted at run time by a request (ability.grant).
    Request,
};

inline constexpr std::uint16_t kAbilityNoGrantEntry = 0xFFFF;

struct AbilityGrant
{
    AbilityGrantId Id;
    AbilityId Ability;
    AbilityGrantKind Kind = AbilityGrantKind::Authored;
    // Kind == Effect: the effect entity holding it.
    EntityId Effect;
    // Its entry in the World's AbilityGrantEntries: key, override, effective binding.
    std::uint16_t Entry = kAbilityNoGrantEntry;
};

// What an actor may run.
struct SENCHA_COMPONENT("sencha.ability_grants") AbilityGrants
{
    AbilityGrant Entries[kAbilityGrantCapacity] = {};
    std::uint8_t Count = 0;
    std::uint32_t NextId = 1;

    [[nodiscard]] const AbilityGrant* Find(AbilityGrantId id) const;
};

static_assert(std::is_trivially_copyable_v<AbilityGrants>,
              "AbilityGrants must be trivially copyable to live in ECS chunks");

// An actor that may run abilities runs them on its own clock, holds what it runs
// and its tags.
template<>
struct ComponentTraits<AbilityGrants>
{
    using DerivedComponents =
        std::tuple<ActiveAbilities, AbilityLocals, AbilityCueLog, AbilityPresentationState, ActorTime,
                   GameplayTagContainer>;
};

// Invalid when the component is full or `ability` is invalid.
AbilityGrantId GrantAbility(AbilityGrants& grants, AbilityId ability, AbilityGrantKind kind, std::uint16_t entry,
                            EntityId effect = {});
bool RevokeAbilityGrant(AbilityGrants& grants, AbilityGrantId id);

#if !defined(SENCHA_CODEGEN)
#  include <abilities/AbilityGrants.sencha.h>
#  include <abilities/AbilityGrantsWire.h>
#endif
