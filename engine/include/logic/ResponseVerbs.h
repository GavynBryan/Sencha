#pragma once

#include <authored/AuthoredEventRegistry.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class FixedSimulationLoop;
class World;

inline constexpr std::string_view kResponsesEnableVerb = "responses.enable";
inline constexpr std::string_view kResponsesDisableVerb = "responses.disable";
// Every interaction-answering volume the activator stands in, at the next pass.
inline constexpr std::string_view kTriggerInteractVerb = "trigger.interact";

// The verbs, and the sensor occurrences a volume's responses answer
// (logic/ResponsesSystem.h).
void DeclareResponseVocabulary(VerbRegistrationScope& verbs, AuthoredEventRegistrationScope& events);

// Enabling and disabling write a binding's state on the authority -- one
// binding by its key, or all of them. An interaction is admitted into the
// World's SensorInteractions queue.
class ResponseOperations
{
public:
    ResponseOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    VerbAdmission Enable(const VerbInvocation& invocation);
    VerbAdmission Disable(const VerbInvocation& invocation);
    VerbAdmission Interact(const VerbInvocation& invocation);

private:
    VerbAdmission SetEnabled(const VerbInvocation& invocation, bool enabled);

    World* Entities;
    const FixedSimulationLoop* Clock;
};

inline VerbAdmission InvokeResponsesEnable(ResponseOperations& operations, const VerbInvocation& invocation)
{
    return operations.Enable(invocation);
}

inline VerbAdmission InvokeResponsesDisable(ResponseOperations& operations, const VerbInvocation& invocation)
{
    return operations.Disable(invocation);
}

inline VerbAdmission InvokeTriggerInteract(ResponseOperations& operations, const VerbInvocation& invocation)
{
    return operations.Interact(invocation);
}
