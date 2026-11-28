#pragma once

#include <logic/Counter.h>
#include <logic/decision/DecisionAgent.h>
#include <logic/EffectReactions.h>
#include <logic/Responses.h>
#include <logic/ResponsesSerializer.h>
#include <logic/VerbRelay.h>
#include <logic/VerbRelaySerializer.h>
#include <world/ComponentRegistrar.h>
#include <world/serialization/ComponentSerializer.h>

#include <memory>

// Authored logic and the state it keeps: relays, which a native path activates,
// responses, which answer the occurrences delivered at their entity, effect
// reactions, which answer the effects applied to it, and counters, the named
// numbers level data sets and tests.
using LogicComponents = ComponentSet<VerbRelay, Responses, EffectReactions, DecisionAgent, Counter>;

inline void RegisterLogicComponents(ComponentRegistrar& registrar)
{
    // A counter's name is required, so a default counter is not a record of its own.
    registrar.AddSerializer(std::make_unique<ComponentSerializerWithoutDefault<Counter>>());
    registrar.AddAll<LogicComponents>();
    // A relay's key persists as text and its set as a path, and responses as
    // their authored bindings: state kept off the row, so hand-written.
    registrar.AddSerializer(MakeVerbRelaySerializer());
    registrar.AddSerializer(MakeResponsesSerializer());
}

void RegisterLogicRelocation(World& world);
