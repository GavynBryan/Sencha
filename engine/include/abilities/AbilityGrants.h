#pragma once

#include <abilities/AbilityCapacity.h>
#include <abilities/AbilityCueLog.h>
#include <abilities/AbilityGrantId.h>
#include <abilities/AbilityId.h>
#include <abilities/AbilityPresentationState.h>
#include <abilities/ActiveAbilities.h>
#include <core/containers/ListPool.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <time/ActorTime.h>
#include <world/identity/PersistableRef.h>

#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

class World;

enum class AbilityGrantKind : std::uint8_t
{
    // A grant entry authored on the actor.
    Authored,
    // Held by a lasting effect for as long as it lasts.
    Effect,
    // Granted at run time by a request (ability.grant).
    Request,
    Transient,
};

inline constexpr std::uint16_t kAbilityNoGrantEntry = 0xFFFF;

struct AbilityGrant
{
    AbilityGrantId Id;
    AbilityId Ability;
    AbilityGrantKind Kind = AbilityGrantKind::Authored;
    // Set for Kind == Effect on the authority alone.
    EntityId Holder;
    PersistableRef Source;
    // Its entry in the World's AbilityGrantEntries: key, override, effective binding.
    std::uint16_t Entry = kAbilityNoGrantEntry;
};

using AbilityGrantLists = ListPool<struct AbilityGrantListTag, AbilityGrant>;

struct SENCHA_COMPONENT("sencha.ability_grants") AbilityGrants
{
    AbilityGrantLists::HandleType Entries;
    std::uint16_t Count = 0;
    // Asked of every actor every tick, so answered without reading the list.
    std::uint16_t EffectHeld = 0;
    std::uint16_t Transient = 0;
    std::uint32_t NextId = 1;
};

static_assert(std::is_trivially_copyable_v<AbilityGrants>,
              "AbilityGrants must be trivially copyable to live in ECS chunks");

// An actor that may run abilities runs them on its own clock, holds what it runs
// and its tags.
template<>
struct ComponentTraits<AbilityGrants>
{
    using DerivedComponents =
        std::tuple<ActiveAbilities, AbilityCueLog, AbilityPresentationState,
                   ActorTime, GameplayTagContainer>;

    static void OnAdd(AbilityGrants& grants, World& world, EntityId actor);
    static void OnRemove(const AbilityGrants& grants, World& world, EntityId actor);
};

struct AbilityGrantList
{
    std::vector<AbilityGrant> Entries;
    std::uint32_t NextId = 1;
};

[[nodiscard]] std::span<const AbilityGrant> AbilityGrantsOf(const World& world, const AbilityGrants& grants);
[[nodiscard]] std::span<const AbilityGrant> AbilityGrantsOf(const World& world, EntityId actor);
[[nodiscard]] const AbilityGrant* FindAbilityGrant(std::span<const AbilityGrant> grants, AbilityGrantId id);

// Invalid when the list is full or `ability` is invalid; on a World, also when
// `actor` carries no AbilityGrants.
AbilityGrantId GrantAbility(AbilityGrantList& grants, AbilityId ability, AbilityGrantKind kind, std::uint16_t entry,
                            EntityId holder = {}, PersistableRef source = {});
AbilityGrantId GrantAbility(World& world, EntityId actor, AbilityId ability, AbilityGrantKind kind, std::uint16_t entry,
                            EntityId holder = {}, PersistableRef source = {});
bool RevokeAbilityGrant(AbilityGrantList& grants, AbilityGrantId id);
bool RevokeAbilityGrant(World& world, EntityId actor, AbilityGrantId id);

[[nodiscard]] AbilityGrantList CopyAbilityGrants(const World& world, EntityId actor);
[[nodiscard]] AbilityGrants MakeAbilityGrants(World& world, const AbilityGrantList& grants);
void SetAbilityGrants(World& world, EntityId actor, const AbilityGrantList& grants);

#if !defined(SENCHA_CODEGEN)
#  include <abilities/AbilityGrants.sencha.h>
#  include <abilities/AbilityGrantsWire.h>
#endif
