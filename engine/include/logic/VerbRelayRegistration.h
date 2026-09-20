#pragma once

#include <logic/VerbRelay.h>
#include <world/ComponentRegistrar.h>

// Authored logic placed in a scene. One member today; the set exists so the
// engine's roster names this feature the way it names every other, and a
// second placed-logic component lands here rather than beside a transform.
using LogicComponents = ComponentSet<VerbRelay>;

inline void RegisterLogicComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<LogicComponents>();
}
