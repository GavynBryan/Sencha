#include "TemplateGame.h"

#include "PlayerStartComponent.h"
#include "SpinComponent.h"
#include "TurretMount.h"

#include <world/ComponentRegistrar.h>

// Every component this game owns, named once. Storage, a scene serializer, and
// a place in the replicated table follow from what each component's TypeSchema
// declares -- there is no second list to keep in step, and nothing to write
// again to take them back.
void TemplateGame::OnRegisterComponents(ComponentRegistrar& registrar)
{
    registrar.Add<SpinComponent>();
    registrar.Add<PlayerStartComponent>();
    registrar.Add<TurretMount>();
    // Authority-only: it holds a runtime entity handle, so it neither travels
    // nor is written down.
    registrar.Add<TurretSeat>();
}
