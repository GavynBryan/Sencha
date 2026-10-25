#include <logic/ResponseVerbs.h>

#include <authored/AuthoredValue.h>
#include <core/metadata/ComponentDefinition.h>
#include <ecs/World.h>
#include <logic/ResponseSet.h>
#include <logic/Responses.h>
#include <logic/ResponsesSystem.h>
#include <time/SimClock.h>
#include <world/SimulationAuthority.h>

#include <string>
#include <utility>

namespace
{
VerbDefinition ResponseVerb(std::string_view name, std::string displayName, std::string description)
{
    VerbDefinition verb;
    verb.Name = std::string(name);
    verb.DisplayName = std::move(displayName);
    verb.Description = std::move(description);
    verb.Category = "Responses";
    return verb;
}

void AddBindingArguments(VerbDefinition& verb)
{
    DataFieldSchema entity = MakeDataField(DataFieldKind::Entity, "entity", "Entity", "The entity whose responses change.");
    entity.Reference.ComponentIdentity = std::string(ComponentDefinition<Responses>::Identity);
    verb.Arguments.Children.push_back(std::move(entity));
    DataFieldSchema binding =
        MakeDataField(DataFieldKind::String, "binding", "Binding", "The binding's key; empty for all of them.");
    binding.Required = false;
    binding.Default = std::string();
    verb.Arguments.Children.push_back(std::move(binding));
}

AuthoredEventDefinition SensorEvent(std::string_view name, std::string displayName, std::string description)
{
    AuthoredEventDefinition event;
    event.Name = std::string(name);
    event.DisplayName = std::move(displayName);
    event.Description = std::move(description);
    event.Category = "Sensor";
    event.Payload.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "activator", "Activator", "The body or character the volume sensed."));
    event.Instigator = "activator";
    return event;
}
} // namespace

void DeclareResponseVocabulary(VerbRegistrationScope& verbs, AuthoredEventRegistrationScope& events)
{
    VerbDefinition enable = ResponseVerb(kResponsesEnableVerb, "Enable response", "Lets a binding respond again.");
    AddBindingArguments(enable);
    (void)verbs.Declare(std::move(enable));

    VerbDefinition disable =
        ResponseVerb(kResponsesDisableVerb, "Disable response", "Stops a binding responding until it is enabled.");
    AddBindingArguments(disable);
    (void)verbs.Declare(std::move(disable));

    VerbDefinition interact = ResponseVerb(kTriggerInteractVerb, "Interact",
                                           "Interacts with every trigger volume the activator stands in.");
    interact.Arguments.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "activator", "Activator", "Who is interacting."));
    (void)verbs.Declare(std::move(interact));

    (void)events.Declare(SensorEvent(kSensorEnterEvent, "Enter", "Something entered the volume."));
    (void)events.Declare(
        SensorEvent(kSensorStayEvent, "Stay", "Something is inside the volume: every sample, its entry included."));
    (void)events.Declare(SensorEvent(kSensorExitEvent, "Exit", "Something left the volume."));
    (void)events.Declare(
        SensorEvent(kSensorInteractEvent, "Interact", "Something inside the volume asked to interact."));
}

VerbAdmission ResponseOperations::SetEnabled(const VerbInvocation& invocation, bool enabled)
{
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    EntityId target;
    std::string_view key;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, target)
        || !invocation.Arguments->TryGetString(1, key))
        return VerbAdmission::InvalidArguments;
    Responses* responses = Entities->IsRegistered<Responses>() && Entities->IsAlive(target)
        ? Entities->TryGet<Responses>(target)
        : nullptr;
    const ResponseDefinitions* definitions = Entities->TryGetResource<ResponseDefinitions>();
    if (responses == nullptr || definitions == nullptr)
        return VerbAdmission::Refused;

    const std::span<const ResponseBindingDesc> bindings = definitions->Sets.Bindings(responses->Set);
    std::uint32_t mask = 0;
    if (key.empty())
        mask = bindings.size() >= 32 ? ~0u : (1u << bindings.size()) - 1u;
    else if (const int index = FindResponseBinding(bindings, ResponseKeyHash(key)); index >= 0)
        mask = 1u << static_cast<std::uint32_t>(index);
    else
        return VerbAdmission::InvalidArguments;
    responses->Disabled = enabled ? (responses->Disabled & ~mask) : (responses->Disabled | mask);
    return VerbAdmission::Accepted;
}

VerbAdmission ResponseOperations::Enable(const VerbInvocation& invocation)
{
    return SetEnabled(invocation, true);
}

VerbAdmission ResponseOperations::Disable(const VerbInvocation& invocation)
{
    return SetEnabled(invocation, false);
}

VerbAdmission ResponseOperations::Interact(const VerbInvocation& invocation)
{
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    EntityId activator;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, activator))
        return VerbAdmission::InvalidArguments;
    SensorInteractions* requests = Entities->TryGetResource<SensorInteractions>();
    if (requests == nullptr || !Entities->IsAlive(activator))
        return VerbAdmission::Refused;
    const std::uint64_t tick = invocation.Tick != 0 ? invocation.Tick : Clock->GetTickIndex();
    return requests->Admit(activator, tick) ? VerbAdmission::Accepted : VerbAdmission::QueueFull;
}
