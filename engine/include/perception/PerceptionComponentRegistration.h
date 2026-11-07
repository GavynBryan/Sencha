#pragma once

#include <perception/ObservationMemory.h>
#include <world/ComponentRegistrar.h>
#include <world/ComponentSet.h>

// Memory lives on the authority only, so nothing here replicates.
using PerceptionComponents = ComponentSet<ObservationMemory>;

inline void RegisterPerceptionComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<PerceptionComponents>();
}
