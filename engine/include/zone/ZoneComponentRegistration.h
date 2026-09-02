#pragma once

#include <world/ComponentRegistrar.h>
#include <zone/WorldConnectionComponents.h>

// How one zone reaches another: the authored attachment points and the links
// between them, plus what gates a link.
using ZoneComponents = ComponentSet<WorldDock, WorldLink, DockGateBinding>;

inline void RegisterZoneComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<ZoneComponents>();
}
