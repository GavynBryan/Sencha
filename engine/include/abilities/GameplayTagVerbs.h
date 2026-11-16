#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class World;

// Give or take stacks of a tag an entity keeps: its baseline, which saves, as
// level data's permanent grants (an unlock) need. Never a projected tag.
inline constexpr std::string_view kTagsGrantVerb = "tags.grant";
inline constexpr std::string_view kTagsRevokeVerb = "tags.revoke";

void DeclareGameplayTagVerbs(VerbRegistrationScope& scope);

// Both run on the simulation authority only. A grant of a tag new to a full
// container is refused and counted (GameplayTagOverflow).
class GameplayTagOperations
{
public:
    explicit GameplayTagOperations(World& world)
        : Entities(&world)
    {
    }

    VerbAdmission Grant(const VerbInvocation& invocation);
    VerbAdmission Revoke(const VerbInvocation& invocation);

private:
    World* Entities;
};

inline VerbAdmission InvokeTagsGrant(GameplayTagOperations& operations, const VerbInvocation& invocation)
{
    return operations.Grant(invocation);
}

inline VerbAdmission InvokeTagsRevoke(GameplayTagOperations& operations, const VerbInvocation& invocation)
{
    return operations.Revoke(invocation);
}
