#pragma once

#include <perception/HearingSensor.h>
#include <perception/ObservationMemory.h>
#include <perception/ObservationSharing.h>
#include <world/ComponentRegistrar.h>
#include <world/ComponentSet.h>

// Ears, memory and sharing serve the authority only, so nothing here replicates.
using PerceptionComponents = ComponentSet<HearingSensor, ObservationMemory, ObservationSharing>;

inline void RegisterPerceptionComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<PerceptionComponents>();
}
