#pragma once

#include <logic/Counter.h>
#include <logic/CounterSerializer.h>
#include <logic/decision/DecisionAgent.h>
#include <logic/EffectReactions.h>
#include <logic/Responses.h>
#include <logic/ResponsesSerializer.h>
#include <logic/VerbRelay.h>
#include <logic/VerbRelaySerializer.h>
#include <world/ComponentRegistrar.h>

// Authored logic and the state it keeps: relays, which a native path activates,
// responses, which answer the occurrences delivered at their entity, effect
// reactions, which answer the effects applied to it, and counters, the named
// numbers level data sets and tests.
using LogicComponents = ComponentSet<VerbRelay, Responses, EffectReactions, DecisionAgent, Counter>;

inline void RegisterLogicComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<LogicComponents>();
    // No scene form here is the component's in-memory form -- a relay's key
    // persists as text and its set as a path, responses as their authored
    // bindings, a counter's name as its tag's name -- so all are hand-written.
    registrar.AddSerializer(MakeVerbRelaySerializer());
    registrar.AddSerializer(MakeResponsesSerializer());
    registrar.AddSerializer(MakeCounterSerializer());
}
