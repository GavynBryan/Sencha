#include "SubtypeEditors.h"

#include "SubtypeEditorRegistry.h"
#include "movement/MovementProfileEditor.h"

void RegisterBuiltInSubtypeEditors(SubtypeEditorRegistry& registry)
{
    (void)registry.Register(CreateMovementProfileEditor());
}
