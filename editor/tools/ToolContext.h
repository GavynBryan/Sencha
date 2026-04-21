#pragma once

class CommandStack;
class PickingService;
class SelectionService;

struct ToolContext
{
    ToolContext(CommandStack& commandStack,
                SelectionService& selectionService,
                PickingService& pickingService);

    CommandStack& Commands;
    SelectionService& Selection;
    PickingService& Picking;
};
