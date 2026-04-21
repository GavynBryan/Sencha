#pragma once

class CommandStack;
class LevelDocument;
class LevelScene;
class PickingService;
class SelectionService;

struct ToolContext
{
    ToolContext(CommandStack& commandStack,
                SelectionService& selectionService,
                PickingService& pickingService,
                LevelScene& levelScene,
                LevelDocument& levelDocument);

    CommandStack& Commands;
    SelectionService& Selection;
    PickingService& Picking;
    LevelScene& Scene;
    LevelDocument& Document;
};
