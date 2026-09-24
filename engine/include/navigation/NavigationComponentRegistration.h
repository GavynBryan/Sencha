#pragma once

#include <navigation/NavLinkComponent.h>
#include <navigation/NavigationGeometry.h>
#include <navigation/NavLinkState.h>
#include <world/ComponentRegistrar.h>

// How an agent may travel through a zone beyond its walkable surface.
using NavigationComponents = ComponentSet<NavLink, NavLinkState, NavigationGeometry>;

inline void RegisterNavigationComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<NavigationComponents>();
}
