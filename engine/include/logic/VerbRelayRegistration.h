#pragma once

#include <logic/VerbRelay.h>
#include <logic/VerbRelaySerializer.h>
#include <world/ComponentRegistrar.h>

// Authored logic placed in a scene. One member today; the set exists so the
// engine's roster names this feature the way it names every other, and a
// second placed-logic component lands here rather than beside a transform.
using LogicComponents = ComponentSet<VerbRelay>;

inline void RegisterLogicComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<LogicComponents>();
    // The key persists as text and the set as a path; neither is the
    // component's in-memory form, so the scene form is hand-written.
    registrar.AddSerializer(MakeVerbRelaySerializer());
}
