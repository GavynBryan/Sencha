#include <gameplay_tags/GameplayTagVerbs.h>

#include <ecs/World.h>
#include <gameplay_tags/BaselineGameplayTags.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/SimulationAuthority.h>

bool GameplayTagOperations::Writable(GameplayTagId tag) const
{
    const GameplayTagRegistry* tags = Entities->TryGetResource<GameplayTagRegistry>();
    return IsSimulationAuthority(*Entities) && tags != nullptr && tags->ProjectedBy(tag).empty()
        && Entities->IsRegistered<GameplayTagContainer>();
}

VerbAdmission GameplayTagOperations::Grant(EntityId target, GameplayTagId tag, std::uint16_t stacks)
{
    if (!tag.IsValid())
        return VerbAdmission::InvalidArguments;
    if (!Writable(tag))
        return VerbAdmission::Refused;
    return GrantBaselineTag(*Entities, target, tag, stacks) == GameplayTagWrite::Full ? VerbAdmission::Refused
                                                                                       : VerbAdmission::Accepted;
}

VerbAdmission GameplayTagOperations::Revoke(EntityId target, GameplayTagId tag, std::uint16_t stacks)
{
    if (!tag.IsValid())
        return VerbAdmission::InvalidArguments;
    if (!Writable(tag))
        return VerbAdmission::Refused;
    return RevokeBaselineTag(*Entities, target, tag, stacks) ? VerbAdmission::Accepted : VerbAdmission::Refused;
}
