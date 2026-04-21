#include "ToolContext.h"

ToolContext::ToolContext(CommandStack& commandStack,
                         SelectionService& selectionService,
                         PickingService& pickingService)
    : Commands(commandStack)
    , Selection(selectionService)
    , Picking(pickingService)
{
}
