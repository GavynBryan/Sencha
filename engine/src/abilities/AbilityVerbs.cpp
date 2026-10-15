#include <abilities/AbilityVerbs.h>

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilityRequestQueue.h>
#include <authored/AuthoredValue.h>
#include <ecs/World.h>

#include <string>
#include <utility>

namespace
{
    DataFieldSchema Arguments(bool keyed)
    {
        DataFieldSchema target = MakeDataField(DataFieldKind::Entity, "target", "Target", "The actor.");
        target.Reference.ComponentIdentity = std::string(ComponentDefinition<AbilityGrants>::Identity);
        DataFieldSchema ability = MakeDataField(DataFieldKind::DataAssetRef, "ability", "Ability");
        ability.Reference.DataSubtype = std::string(kAbilityDefinitionType);
        DataFieldSchema arguments = EmptyVerbArguments();
        arguments.Children = { std::move(target), std::move(ability) };
        if (keyed)
            arguments.Children.push_back(
                MakeDataField(DataFieldKind::String, "key", "Key", "Names the grant, so it can be revoked."));
        return arguments;
    }

    VerbDefinition Verb(std::string_view name, std::string label, std::string description, bool keyed)
    {
        VerbDefinition verb;
        verb.Name = std::string(name);
        verb.DisplayName = std::move(label);
        verb.Description = std::move(description);
        verb.Category = "Abilities";
        verb.Arguments = Arguments(keyed);
        return verb;
    }

    struct Request
    {
        EntityId Target;
        AbilityId Ability;
        std::string_view Key;
    };

    bool Read(World& world, const VerbInvocation& invocation, bool keyed, Request& out)
    {
        const AssetRef* ability = nullptr;
        AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
        if (invocation.Arguments == nullptr || abilities == nullptr
            || !invocation.Arguments->TryGetEntity(0, out.Target) || !invocation.Arguments->TryGetDataAsset(1, ability)
            || (keyed && !invocation.Arguments->TryGetString(2, out.Key)) || !world.IsAlive(out.Target))
            return false;
        out.Ability = abilities->Reserve(ability->Path);
        return out.Ability.IsValid();
    }
}

void DeclareAbilityVerbs(VerbRegistrationScope& scope)
{
    (void)scope.Declare(Verb(kAbilityActivateVerb, "Activate ability",
                             "Starts an ability the actor is granted, if admission allows, in the next ability pass.",
                             false));
    (void)scope.Declare(Verb(kAbilityCancelVerb, "Cancel ability", "Ends the actor's running activations of an ability.",
                             false));
    (void)scope.Declare(Verb(kAbilityGrantVerb, "Grant ability", "Lets the actor run an ability, under a key.", true));
    (void)scope.Declare(Verb(kAbilityRevokeVerb, "Revoke ability", "Takes back an ability granted under a key.", true));
}

VerbAdmission AbilityOperations::Activate(const VerbInvocation& invocation)
{
    Request request;
    AbilityRequestQueue* queue = Entities->TryGetResource<AbilityRequestQueue>();
    if (queue == nullptr || !Read(*Entities, invocation, false, request))
        return VerbAdmission::InvalidArguments;
    return queue->Push({ .Kind = AbilityRequestKind::Activate, .Actor = request.Target, .Ability = request.Ability,
                         .Instigator = invocation.Instigator, .Cause = invocation.Id })
        ? VerbAdmission::Accepted
        : VerbAdmission::QueueFull;
}

VerbAdmission AbilityOperations::Cancel(const VerbInvocation& invocation)
{
    Request request;
    AbilityRequestQueue* queue = Entities->TryGetResource<AbilityRequestQueue>();
    if (queue == nullptr || !Read(*Entities, invocation, false, request))
        return VerbAdmission::InvalidArguments;
    return queue->Push({ .Kind = AbilityRequestKind::Cancel, .Actor = request.Target, .Ability = request.Ability,
                         .Instigator = invocation.Instigator, .Cause = invocation.Id })
        ? VerbAdmission::Accepted
        : VerbAdmission::QueueFull;
}

VerbAdmission AbilityOperations::Grant(const VerbInvocation& invocation)
{
    Request request;
    AbilityGrantEntries* entries = Entities->TryGetResource<AbilityGrantEntries>();
    if (entries == nullptr || !Entities->IsRegistered<AbilityGrants>() || !Read(*Entities, invocation, true, request)
        || request.Key.empty())
        return VerbAdmission::InvalidArguments;
    const std::uint16_t entry = entries->Intern({ std::string(request.Key), request.Ability, {} });
    if (entry == kAbilityNoGrantEntry)
        return VerbAdmission::Refused;
    if (!Entities->HasComponent<AbilityGrants>(request.Target))
        Entities->AddComponent<AbilityGrants>(request.Target, AbilityGrants{});
    return GrantAbility(*Entities->TryGet<AbilityGrants>(request.Target), request.Ability, AbilityGrantKind::Request,
                        entry)
               .IsValid()
        ? VerbAdmission::Accepted
        : VerbAdmission::Refused;
}

VerbAdmission AbilityOperations::Revoke(const VerbInvocation& invocation)
{
    Request request;
    const AbilityGrantEntries* entries = Entities->TryGetResource<AbilityGrantEntries>();
    if (entries == nullptr || !Entities->IsRegistered<AbilityGrants>() || !Read(*Entities, invocation, true, request))
        return VerbAdmission::InvalidArguments;
    AbilityGrants* grants = Entities->TryGet<AbilityGrants>(request.Target);
    for (std::uint8_t i = 0; grants != nullptr && i < grants->Count; ++i)
    {
        const AbilityGrant& grant = grants->Entries[i];
        const AbilityGrantEntry* entry = entries->Find(grant.Entry);
        if (grant.Kind == AbilityGrantKind::Request && grant.Ability == request.Ability && entry != nullptr
            && entry->Key == request.Key)
            return RevokeAbilityGrant(*grants, grant.Id) ? VerbAdmission::Accepted : VerbAdmission::Refused;
    }
    return VerbAdmission::Refused;
}
