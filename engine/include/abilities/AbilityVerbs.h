#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class World;

// Start an ability an entity is granted, by name rather than by input. Admission
// still applies, in the next ability pass.
inline constexpr std::string_view kAbilityActivateVerb = "ability.activate";
// End an entity's running activations of an ability.
inline constexpr std::string_view kAbilityCancelVerb = "ability.cancel";
// Grant or revoke an ability under a key, for scripts and tests; an entity with no
// grants gains them.
inline constexpr std::string_view kAbilityGrantVerb = "ability.grant";
inline constexpr std::string_view kAbilityRevokeVerb = "ability.revoke";

void DeclareAbilityVerbs(VerbRegistrationScope& scope);

class AbilityOperations
{
public:
    explicit AbilityOperations(World& world)
        : Entities(&world)
    {
    }

    VerbAdmission Activate(const VerbInvocation& invocation);
    VerbAdmission Cancel(const VerbInvocation& invocation);
    VerbAdmission Grant(const VerbInvocation& invocation);
    VerbAdmission Revoke(const VerbInvocation& invocation);

private:
    World* Entities;
};

inline VerbAdmission InvokeAbilityActivate(AbilityOperations& operations, const VerbInvocation& invocation)
{
    return operations.Activate(invocation);
}

inline VerbAdmission InvokeAbilityCancel(AbilityOperations& operations, const VerbInvocation& invocation)
{
    return operations.Cancel(invocation);
}

inline VerbAdmission InvokeAbilityGrant(AbilityOperations& operations, const VerbInvocation& invocation)
{
    return operations.Grant(invocation);
}

inline VerbAdmission InvokeAbilityRevoke(AbilityOperations& operations, const VerbInvocation& invocation)
{
    return operations.Revoke(invocation);
}
