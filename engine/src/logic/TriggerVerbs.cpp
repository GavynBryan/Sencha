#include <logic/TriggerVerbs.h>

#include <authored/AuthoredValue.h>
#include <core/metadata/ComponentDefinition.h>
#include <ecs/World.h>
#include <logic/Trigger.h>
#include <logic/TriggerSystem.h>
#include <world/SimulationAuthority.h>

#include <string>
#include <utility>

namespace
{
VerbDefinition TriggerVerb(std::string_view name, std::string displayName, std::string description)
{
    VerbDefinition verb;
    verb.Name = std::string(name);
    verb.DisplayName = std::move(displayName);
    verb.Description = std::move(description);
    verb.Category = "Trigger";
    return verb;
}

DataFieldSchema TriggerArgument()
{
    DataFieldSchema trigger = MakeDataField(DataFieldKind::Entity, "trigger", "Trigger", "The trigger entity.");
    trigger.Reference.ComponentIdentity = std::string(ComponentDefinition<Trigger>::Identity);
    return trigger;
}
} // namespace

void DeclareTriggerVerbs(VerbRegistrationScope& scope)
{
    VerbDefinition enable = TriggerVerb(kTriggerEnableVerb, "Enable trigger", "Lets a trigger activate again.");
    enable.Arguments.Children.push_back(TriggerArgument());
    (void)scope.Declare(std::move(enable));

    VerbDefinition disable =
        TriggerVerb(kTriggerDisableVerb, "Disable trigger", "Stops a trigger activating until it is enabled.");
    disable.Arguments.Children.push_back(TriggerArgument());
    (void)scope.Declare(std::move(disable));

    VerbDefinition interact = TriggerVerb(kTriggerInteractVerb, "Interact",
                                          "Activates every interaction trigger the activator stands in.");
    interact.Arguments.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "activator", "Activator", "Who is interacting."));
    (void)scope.Declare(std::move(interact));
}

VerbAdmission TriggerOperations::SetEnabled(const VerbInvocation& invocation, bool enabled)
{
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    EntityId target;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, target))
        return VerbAdmission::InvalidArguments;
    Trigger* trigger = Entities->IsRegistered<Trigger>() && Entities->IsAlive(target) ? Entities->TryGet<Trigger>(target)
                                                                                      : nullptr;
    if (trigger == nullptr)
        return VerbAdmission::Refused;
    trigger->Enabled = enabled;
    return VerbAdmission::Accepted;
}

VerbAdmission TriggerOperations::Enable(const VerbInvocation& invocation)
{
    return SetEnabled(invocation, true);
}

VerbAdmission TriggerOperations::Disable(const VerbInvocation& invocation)
{
    return SetEnabled(invocation, false);
}

VerbAdmission TriggerOperations::Interact(const VerbInvocation& invocation)
{
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    EntityId activator;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, activator))
        return VerbAdmission::InvalidArguments;
    TriggerInteractions* requests = Entities->TryGetResource<TriggerInteractions>();
    if (requests == nullptr || !Entities->IsAlive(activator))
        return VerbAdmission::Refused;
    return requests->Admit(activator) ? VerbAdmission::Accepted : VerbAdmission::QueueFull;
}
