#pragma once

#include <logic/Trigger.h>
#include <logic/TriggerSerializer.h>
#include <logic/VerbRelay.h>
#include <logic/VerbRelaySerializer.h>
#include <world/ComponentRegistrar.h>

// Authored logic placed in a scene: relays, which a native path activates, and
// triggers, which their sensors do.
using LogicComponents = ComponentSet<VerbRelay, Trigger>;

inline void RegisterLogicComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<LogicComponents>();
    // Neither scene form is the component's in-memory form -- a relay's key
    // persists as text and its set as a path, a trigger's sequence as its
    // authored text -- so both are hand-written.
    registrar.AddSerializer(MakeVerbRelaySerializer());
    registrar.AddSerializer(MakeTriggerSerializer());
}
