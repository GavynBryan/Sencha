#include <logic/ResponseVerbs.h>

#include <authored/AuthoredApi.h>
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
    DeclareAuthoredApi<ResponseOperations>(&verbs, nullptr);
    (void)events.Declare(SensorEvent(kSensorEnterEvent, "Enter", "Something entered the volume."));
    (void)events.Declare(
        SensorEvent(kSensorStayEvent, "Stay", "Something is inside the volume: every sample, its entry included."));
    (void)events.Declare(SensorEvent(kSensorExitEvent, "Exit", "Something left the volume."));
    (void)events.Declare(
        SensorEvent(kSensorInteractEvent, "Interact", "Something inside the volume asked to interact."));
}

VerbAdmission ResponseOperations::SetEnabled(EntityId entity, std::string_view key, bool enabled)
{
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    Responses* responses = Entities->IsRegistered<Responses>() ? Entities->TryGet<Responses>(entity) : nullptr;
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

VerbAdmission ResponseOperations::Enable(EntityId entity, std::string_view binding)
{
    return SetEnabled(entity, binding, true);
}

VerbAdmission ResponseOperations::Disable(EntityId entity, std::string_view binding)
{
    return SetEnabled(entity, binding, false);
}

VerbAdmission ResponseOperations::Interact(const VerbInvocation& invocation, EntityId activator)
{
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    SensorInteractions* requests = Entities->TryGetResource<SensorInteractions>();
    if (requests == nullptr)
        return VerbAdmission::Refused;
    const std::uint64_t tick = Clock->TimeOfTick(invocation.Tick).TickIndex;
    return requests->Admit(activator, tick) ? VerbAdmission::Accepted : VerbAdmission::QueueFull;
}
