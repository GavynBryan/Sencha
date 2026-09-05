#include "PlatformerComponents.h"

#include "PlatformerStart.h"

#include <world/ComponentRegistrar.h>

void RegisterPlatformerComponents(ComponentRegistrar& registrar)
{
    registrar.Add<PlatformerStart>();
}
