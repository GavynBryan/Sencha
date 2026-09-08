#include "HorrorComponents.h"

#include "HorrorStart.h"

#include <world/ComponentRegistrar.h>

void RegisterHorrorComponents(ComponentRegistrar& registrar)
{
    registrar.Add<HorrorStart>();
}
