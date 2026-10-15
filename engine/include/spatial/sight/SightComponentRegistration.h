#pragma once

#include <spatial/sight/SightComponents.h>
#include <world/ComponentRegistrar.h>
#include <world/ComponentSet.h>

// Authored eyes and the points they look for. Neither replicates: sight runs
// on the authority, and what it concludes reaches peers through gameplay.
using SightComponents = ComponentSet<SightSensor, SightTarget>;

inline void RegisterSightComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<SightComponents>();
}
