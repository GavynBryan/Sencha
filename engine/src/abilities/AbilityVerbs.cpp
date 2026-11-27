#include <abilities/AbilityVerbs.h>

#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityRequestQueue.h>
#include <ecs/World.h>

#include <string>

VerbAdmission AbilityOperations::Activate(const VerbInvocation& invocation, EntityId target, AbilityId ability,
                                          std::optional<EntityId> source)
{
    AbilityRequestQueue* queue = Entities->TryGetResource<AbilityRequestQueue>();
    if (queue == nullptr)
        return VerbAdmission::Unavailable;
    queue->Push({ .Kind = AbilityRequestKind::Activate, .Actor = target, .Ability = ability,
                  .Instigator = invocation.Instigator, .Cause = invocation.Id, .Source = source.value_or(EntityId{}) });
    return VerbAdmission::Accepted;
}

VerbAdmission AbilityOperations::Cancel(const VerbInvocation& invocation, EntityId target, AbilityId ability)
{
    AbilityRequestQueue* queue = Entities->TryGetResource<AbilityRequestQueue>();
    if (queue == nullptr)
        return VerbAdmission::Unavailable;
    queue->Push({ .Kind = AbilityRequestKind::Cancel, .Actor = target, .Ability = ability,
                  .Instigator = invocation.Instigator, .Cause = invocation.Id });
    return VerbAdmission::Accepted;
}

VerbAdmission AbilityOperations::Grant(EntityId target, AbilityId ability, std::string_view key,
                                       std::optional<EntityId> source)
{
    AbilityGrantEntries* entries = Entities->TryGetResource<AbilityGrantEntries>();
    if (entries == nullptr || !Entities->IsRegistered<AbilityGrants>())
        return VerbAdmission::Unavailable;
    if (key.empty())
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 2).Status;
    const std::uint16_t entry = entries->Intern({ std::string(key), ability, {} });
    if (entry == kAbilityNoGrantEntry || AbilityGrantRoom(*Entities, target) == 0)
        return VerbAdmission::Refused;
    if (!Entities->HasComponent<AbilityGrants>(target))
        Entities->AddComponent<AbilityGrants>(target, AbilityGrants{});
    return GrantAbility(*Entities, target, ability, AbilityGrantKind::Request, entry, {},
                        MakePersistableRef(*Entities, source.value_or(EntityId{})))
               .IsValid()
        ? VerbAdmission::Accepted
        : VerbAdmission::Refused;
}

VerbAdmission AbilityOperations::Revoke(EntityId target, AbilityId ability, std::string_view key)
{
    const AbilityGrantEntries* entries = Entities->TryGetResource<AbilityGrantEntries>();
    if (entries == nullptr || !Entities->IsRegistered<AbilityGrants>())
        return VerbAdmission::Unavailable;
    for (const AbilityGrant& grant : AbilityGrantsOf(*Entities, target))
    {
        const AbilityGrantEntry* entry = entries->Find(grant.Entry);
        if (grant.Kind == AbilityGrantKind::Request && grant.Ability == ability && entry != nullptr
            && entry->Key == key)
            return RevokeAbilityGrant(*Entities, target, grant.Id) ? VerbAdmission::Accepted : VerbAdmission::Refused;
    }
    return VerbAdmission::Refused;
}
