#include <gameplay_tags/GameplayTagVerbs.h>

#include <authored/AuthoredValue.h>
#include <ecs/World.h>
#include <gameplay_tags/BaselineGameplayTags.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/SimulationAuthority.h>

#include <cstdint>
#include <string>
#include <utility>

namespace
{
    VerbDefinition Verb(std::string_view name, std::string label, std::string description, std::string stacksSummary)
    {
        DataFieldSchema target = MakeDataField(DataFieldKind::Entity, "target", "Target", "The entity that keeps it.");
        target.Reference.ComponentIdentity = std::string(ComponentDefinition<GameplayTagContainer>::Identity);
        DataFieldSchema stacks = MakeDataField(DataFieldKind::Int, "stacks", "Stacks", std::move(stacksSummary));
        stacks.Default = std::int64_t{ 1 };
        stacks.Numeric.Minimum = 1.0;
        stacks.Numeric.Maximum = 65535.0;
        VerbDefinition verb;
        verb.Name = std::string(name);
        verb.DisplayName = std::move(label);
        verb.Description = std::move(description);
        verb.Category = "Tags";
        verb.Arguments.Children = { std::move(target), MakeDataField(DataFieldKind::GameplayTag, "tag", "Tag"),
                                    std::move(stacks) };
        return verb;
    }

    struct Request
    {
        EntityId Target;
        GameplayTagId Tag;
        std::uint16_t Stacks = 1;
    };

    VerbAdmission Read(const World& world, const VerbInvocation& invocation, Request& out)
    {
        std::int64_t stacks = 1;
        if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, out.Target)
            || !invocation.Arguments->TryGetTag(1, out.Tag) || !invocation.Arguments->TryGetInt(2, stacks)
            || stacks < 1 || stacks > 0xFFFF || !out.Tag.IsValid() || !world.IsAlive(out.Target))
            return VerbAdmission::InvalidArguments;
        out.Stacks = static_cast<std::uint16_t>(stacks);
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        if (!IsSimulationAuthority(world) || tags == nullptr || !tags->ProjectedBy(out.Tag).empty()
            || !world.IsRegistered<GameplayTagContainer>())
            return VerbAdmission::Refused;
        return VerbAdmission::Accepted;
    }
}

void DeclareGameplayTagVerbs(VerbRegistrationScope& scope)
{
    (void)scope.Declare(Verb(kTagsGrantVerb, "Grant tag",
                             "Gives the target stacks of a tag it keeps and saves; an entity with no tags gains them.",
                             "How many stacks to add."));
    (void)scope.Declare(Verb(kTagsRevokeVerb, "Revoke tag",
                             "Takes stacks of a tag the target keeps; never what an effect or ability holds.",
                             "How many stacks to take, at most what it keeps."));
}

VerbAdmission GameplayTagOperations::Grant(const VerbInvocation& invocation)
{
    Request request;
    if (const VerbAdmission read = Read(*Entities, invocation, request); read != VerbAdmission::Accepted)
        return read;
    return GrantBaselineTag(*Entities, request.Target, request.Tag, request.Stacks) == GameplayTagWrite::Full
        ? VerbAdmission::Refused
        : VerbAdmission::Accepted;
}

VerbAdmission GameplayTagOperations::Revoke(const VerbInvocation& invocation)
{
    Request request;
    if (const VerbAdmission read = Read(*Entities, invocation, request); read != VerbAdmission::Accepted)
        return read;
    return RevokeBaselineTag(*Entities, request.Target, request.Tag, request.Stacks) ? VerbAdmission::Accepted
                                                                                    : VerbAdmission::Refused;
}
