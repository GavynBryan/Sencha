#include "TemplateComponents.h"

#include "ObserverFlight.h"
#include "PlayerStartComponent.h"
#include "SpinComponent.h"
#include "TurretMount.h"

#include <world/ComponentRegistrar.h>

void RegisterTemplateComponents(ComponentRegistrar& registrar)
{
    registrar.Add<SpinComponent>();
    registrar.Add<PlayerStartComponent>();
    registrar.Add<TurretMount>();
    registrar.Add<ObserverFlight>();
}
