#pragma once

#include <perception/HearingSensor.h>
#include <perception/ObservationMemory.h>
#include <world/ComponentRegistrar.h>
#include <world/ComponentSet.h>

// Ears and memory serve the authority only, so nothing here replicates.
using PerceptionComponents = ComponentSet<HearingSensor, ObservationMemory>;

inline void RegisterPerceptionComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<PerceptionComponents>();
}
