#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class World;

inline constexpr std::string_view kTriggerEnableVerb = "trigger.enable";
inline constexpr std::string_view kTriggerDisableVerb = "trigger.disable";
// Every Interaction trigger the activator stands in, answered at the next trigger pass.
inline constexpr std::string_view kTriggerInteractVerb = "trigger.interact";

void DeclareTriggerVerbs(VerbRegistrationScope& scope);

// Arming and disarming write the trigger's Enabled flag on the authority. An
// interaction is admitted into the World's TriggerInteractions queue.
class TriggerOperations
{
public:
    explicit TriggerOperations(World& world)
        : Entities(&world)
    {
    }

    VerbAdmission Enable(const VerbInvocation& invocation);
    VerbAdmission Disable(const VerbInvocation& invocation);
    VerbAdmission Interact(const VerbInvocation& invocation);

private:
    VerbAdmission SetEnabled(const VerbInvocation& invocation, bool enabled);

    World* Entities;
};

inline VerbAdmission InvokeTriggerEnable(TriggerOperations& operations, const VerbInvocation& invocation)
{
    return operations.Enable(invocation);
}

inline VerbAdmission InvokeTriggerDisable(TriggerOperations& operations, const VerbInvocation& invocation)
{
    return operations.Disable(invocation);
}

inline VerbAdmission InvokeTriggerInteract(TriggerOperations& operations, const VerbInvocation& invocation)
{
    return operations.Interact(invocation);
}
