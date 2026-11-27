#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <ecs/EntityId.h>
#include <logic/Responses.h>

#include <string_view>

class FixedSimulationLoop;
class World;

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

    SENCHA_VERB("responses.enable")
    SENCHA_LABEL("Enable response")
    SENCHA_CATEGORY("Responses")
    SENCHA_DESCRIPTION("Lets a binding respond again.")
    VerbAdmission Enable(SENCHA_TARGET("entity", Responses) SENCHA_LABEL("Entity")
                             SENCHA_DESCRIPTION("The entity whose responses change.") EntityId entity,
                         SENCHA_ARG("binding") SENCHA_LABEL("Binding")
                             SENCHA_DESCRIPTION("The binding's key; empty for all of them.") std::string_view binding = "");

    SENCHA_VERB("responses.disable")
    SENCHA_LABEL("Disable response")
    SENCHA_CATEGORY("Responses")
    SENCHA_DESCRIPTION("Stops a binding responding until it is enabled.")
    VerbAdmission Disable(SENCHA_TARGET("entity", Responses) SENCHA_LABEL("Entity")
                              SENCHA_DESCRIPTION("The entity whose responses change.") EntityId entity,
                          SENCHA_ARG("binding") SENCHA_LABEL("Binding")
                              SENCHA_DESCRIPTION("The binding's key; empty for all of them.") std::string_view binding = "");

    SENCHA_VERB("trigger.interact")
    SENCHA_LABEL("Interact")
    SENCHA_CATEGORY("Responses")
    SENCHA_DESCRIPTION("Interacts with every trigger volume the activator stands in.")
    VerbAdmission Interact(const VerbInvocation& invocation,
                           SENCHA_TARGET("activator") SENCHA_LABEL("Activator") SENCHA_DESCRIPTION("Who is interacting.")
                               EntityId activator);

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    VerbAdmission SetEnabled(EntityId entity, std::string_view binding, bool enabled);

    World* Entities;
    const FixedSimulationLoop* Clock;
};

#if !defined(SENCHA_CODEGEN)
#  include <logic/ResponseVerbs.sencha.h>
#endif
