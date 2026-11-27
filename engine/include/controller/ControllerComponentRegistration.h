#pragma once

#include <controller/AimOrigin.h>
#include <controller/LookOrientation.h>
#include <world/ComponentRegistrar.h>

// Where a controlled entity is aiming and from where, the marker naming the one
// this machine's look action drives, and the opt-in that turns a body to face its
// aim. The aim replicates; the markers and the origin are facts about this machine
// or the thing, and never leave it.
using ControllerComponents = ComponentSet<LookOrientation, LocalLookControl, AimFacing, AimOrigin>;

inline void RegisterControllerComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<ControllerComponents>();
}

// How an aim follows a relocation: its yaw turns with a mapping that keeps world
// up and is kept under one that does not (docs/gameplay/relocation.md).
void RegisterControllerRelocation(World& world);
