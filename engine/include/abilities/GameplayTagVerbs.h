#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class World;

inline constexpr std::string_view kTagsGrantVerb = "tags.grant";
inline constexpr std::string_view kTagsRevokeVerb = "tags.revoke";

void DeclareGameplayTagVerbs(VerbRegistrationScope& scope);

// Both run on the simulation authority only.
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
