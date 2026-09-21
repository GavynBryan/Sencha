#include "ArenaComponents.h"

#include "ArenaScoreboard.h"
#include "ArenaStart.h"
#include "samples/turret/TurretMount.h"

#include <world/ComponentRegistrar.h>

void RegisterArenaComponents(ComponentRegistrar& registrar)
{
    registrar.Add<ArenaStart>();
    registrar.Add<ArenaScoreboard>();
    registrar.Add<TurretMount>();
}
