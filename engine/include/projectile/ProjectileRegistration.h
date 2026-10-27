#pragma once

#include <projectile/Projectile.h>
#include <projectile/ProjectileState.h>
#include <world/ComponentRegistrar.h>

// A projectile prefab's authored definition, the flight every machine knows of,
// and the authority's working state while it flies.
using ProjectileComponents = ComponentSet<Projectile, ProjectileFlight, ProjectileMotion>;

inline void RegisterProjectileComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<ProjectileComponents>();
}
