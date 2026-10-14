#include "SubtypeEditors.h"

#include "data/DataSubtypeAuthoring.h"
#include "input/InputActionSetAuthoring.h"
#include "input/InputProfileAuthoring.h"
#include "movement/MovementProfileAuthoring.h"

void RegisterBuiltInSubtypeAuthoring(DataSubtypeAuthoringRegistry& registry)
{
    (void)registry.Register(CreateMovementProfileAuthoring());
    (void)registry.Register(CreateInputActionSetAuthoring());
    (void)registry.Register(CreateInputProfileAuthoring());
}
