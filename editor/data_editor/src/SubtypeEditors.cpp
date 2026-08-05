#include "SubtypeEditors.h"

#include "SubtypeEditorRegistry.h"
#include "input/InputActionSetEditor.h"
#include "movement/MovementProfileEditor.h"

void RegisterBuiltInSubtypeEditors(SubtypeEditorRegistry& registry)
{
    (void)registry.Register(CreateMovementProfileEditor());
    (void)registry.Register(CreateInputActionSetEditor());
}
