#include "SubtypeEditors.h"

#include "ui/DataSubtypeEditorRegistry.h"
#include "input/InputActionSetEditor.h"
#include "input/InputProfileEditor.h"
#include "movement/MovementProfileEditor.h"

void RegisterBuiltInSubtypeEditors(DataSubtypeEditorRegistry& registry)
{
    (void)registry.Register(CreateMovementProfileEditor());
    (void)registry.Register(CreateInputActionSetEditor());
    (void)registry.Register(CreateInputProfileEditor());
}
