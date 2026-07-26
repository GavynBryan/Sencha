#include "TemplateGame.h"

#include "PlayerStartComponent.h"
#include "SpinComponent.h"

#include <ecs/WorldComponentSchema.h>

void TemplateGame::OnRegisterRuntimeComponents(WorldComponentSchema& schema)
{
    schema.Add<SpinComponent>();
    schema.Add<PlayerStartComponent>();
}
