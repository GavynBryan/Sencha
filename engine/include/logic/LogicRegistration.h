#pragma once

#include <logic/Responses.h>
#include <logic/ResponsesSerializer.h>
#include <logic/VerbRelay.h>
#include <logic/VerbRelaySerializer.h>
#include <world/ComponentRegistrar.h>

// Authored logic placed in a scene: relays, which a native path activates, and
// responses, which answer the occurrences delivered at their entity.
using LogicComponents = ComponentSet<VerbRelay, Responses>;

inline void RegisterLogicComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<LogicComponents>();
    // Neither scene form is the component's in-memory form -- a relay's key
    // persists as text and its set as a path, responses as their authored
    // bindings -- so both are hand-written.
    registrar.AddSerializer(MakeVerbRelaySerializer());
    registrar.AddSerializer(MakeResponsesSerializer());
}
