#include "SubtypeEditors.h"

#include "ui/DataSubtypeEditorRegistry.h"
#include "data/DataSubtypeAuthoring.h"
#include "input/InputActionSetAuthoring.h"
#include "input/InputProfileAuthoring.h"
#include "movement/MovementProfileEditor.h"

void RegisterBuiltInSubtypeEditors(DataSubtypeEditorRegistry& registry)
{
    (void)registry.Register(CreateMovementProfileEditor());
}

void RegisterBuiltInSubtypeAuthoring(DataSubtypeAuthoringRegistry& registry)
{
    (void)registry.Register(CreateInputActionSetAuthoring());
    (void)registry.Register(CreateInputProfileAuthoring());
}
