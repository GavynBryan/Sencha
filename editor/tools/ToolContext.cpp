#include "ToolContext.h"

ToolContext::ToolContext(CommandStack& commandStack,
                         SelectionService& selectionService,
                         PickingService& pickingService,
                         LevelScene& levelScene,
                         LevelDocument& levelDocument)
    : Commands(commandStack)
    , Selection(selectionService)
    , Picking(pickingService)
    , Scene(levelScene)
    , Document(levelDocument)
{
}
