#include "ToolContext.h"

ToolContext::ToolContext(CommandStack& commandStack,
                         SelectionService& selectionService,
                         PickingService& pickingService,
                         LevelScene& levelScene,
                         LevelDocument& levelDocument,
                         InteractionHost& interactions,
                         PreviewBuffer& preview)
    : Commands(commandStack)
    , Selection(selectionService)
    , Picking(pickingService)
    , Scene(levelScene)
    , Document(levelDocument)
    , Interactions(interactions)
    , Preview(preview)
{
}
