#include <logic/ClaimVocabulary.h>

#include <authored/AuthoredApi.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/Claims.h>
#include <world/SimulationAuthority.h>

VerbAdmission ClaimOperations::Hold(const VerbInvocation& invocation, GameplayTagId group, GameplayTagId claim,
                                    std::uint32_t count, float share)
{
    ClaimBoard* board = Entities->TryGetResource<ClaimBoard>();
    if (board == nullptr)
        return VerbAdmission::Unavailable;
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    const GameplayTagRegistry* tags = Entities->TryGetResource<GameplayTagRegistry>();
    const std::uint32_t members = tags != nullptr ? CountGroupMembers(*Entities, group, *tags) : 0;
    return board->Take(*Entities, group, claim, invocation.Owner, ClaimCapacity(count, share, members))
        ? VerbAdmission::Accepted
        : VerbAdmission::Refused;
}

VerbAdmission ClaimOperations::EndHold(const VerbInvocation& invocation)
{
    ClaimBoard* board = Entities->TryGetResource<ClaimBoard>();
    if (board == nullptr)
        return VerbAdmission::Unavailable;
    board->ReleaseAll(invocation.Owner);
    return VerbAdmission::Accepted;
}

std::optional<std::int64_t> ClaimOperations::Count(GameplayTagId group, GameplayTagId claim) const
{
    const ClaimBoard* board = Entities->TryGetResource<ClaimBoard>();
    if (board == nullptr)
        return std::nullopt;
    return board->Count(*Entities, group, claim);
}

std::optional<bool> ClaimOperations::Held(EntityId holder, GameplayTagId group, GameplayTagId claim) const
{
    const ClaimBoard* board = Entities->TryGetResource<ClaimBoard>();
    if (board == nullptr)
        return std::nullopt;
    return board->Holds(*Entities, group, claim, holder);
}

bool InstallClaimVocabulary(World& world)
{
    if (!world.HasResource<ClaimBoard>())
        world.AddResource<ClaimBoard>();
    AuthoredQueryRegistry* queries = FindAuthoredQueryRegistry(world);
    VerbRegistry* verbs = FindVerbRegistry(world);
    if (queries == nullptr || verbs == nullptr)
        return false;
    if (queries->Find(AuthoredApiDefinition<ClaimOperations>::Name_Count).IsValid())
        return true;

    AuthoredQueryRegistrationScope queryScope(*queries, "engine");
    DeclareAuthoredApi<ClaimOperations>(nullptr, &queryScope);
    if (!queryScope.Commit())
        return false;
    VerbRegistrationScope verbScope(*verbs, "engine");
    DeclareAuthoredApi<ClaimOperations>(&verbScope, nullptr);
    return verbScope.Commit();
}
