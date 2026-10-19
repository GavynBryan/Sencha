#pragma once

#include <logic/VerbRelay.h>
#include <logic/Trigger.h>
#include <logic/TriggerSerializer.h>
#include <logic/VerbRelaySerializer.h>
#include <world/ComponentRegistrar.h>

using LogicComponents = ComponentSet<VerbRelay, Trigger>;

inline void RegisterLogicComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<LogicComponents>();
    // The key persists as text and the set as a path; neither is the
    // component's in-memory form, so the scene form is hand-written.
    registrar.AddSerializer(MakeVerbRelaySerializer());
    registrar.AddSerializer(MakeTriggerSerializer());
}
