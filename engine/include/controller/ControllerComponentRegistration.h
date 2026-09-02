#pragma once

#include <controller/LookOrientation.h>
#include <world/ComponentRegistrar.h>

// Where a controlled entity is aiming, the marker naming the one this machine's
// look action drives, and the opt-in that turns a body to face its aim. The aim
// replicates; the two markers are facts about this machine and never leave it.
using ControllerComponents = ComponentSet<LookOrientation, LocalLookControl, AimFacing>;

inline void RegisterControllerComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<ControllerComponents>();
}
