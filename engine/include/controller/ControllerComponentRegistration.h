#pragma once

#include <controller/LookOrientation.h>
#include <world/ComponentRegistrar.h>

// Where a controlled entity is aiming, and the marker naming the one this
// machine's look action drives. The aim replicates; which entity is local is a
// fact about this machine and never leaves it.
inline void RegisterControllerComponents(ComponentRegistrar& registrar)
{
    registrar.Add<LookOrientation>();
    registrar.Add<LocalLookControl>();
}
