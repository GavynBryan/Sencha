#include "FpsComponents.h"

#include "FpsStart.h"
#include "TurretMount.h"

#include <world/ComponentRegistrar.h>

void RegisterFpsComponents(ComponentRegistrar& registrar)
{
    registrar.Add<FpsStart>();
    registrar.Add<TurretMount>();
}
