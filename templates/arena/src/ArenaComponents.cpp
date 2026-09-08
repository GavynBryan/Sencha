#include "ArenaComponents.h"

#include "ArenaStart.h"
#include "samples/turret/TurretMount.h"

#include <world/ComponentRegistrar.h>

void RegisterArenaComponents(ComponentRegistrar& registrar)
{
    registrar.Add<ArenaStart>();
    registrar.Add<TurretMount>();
}
