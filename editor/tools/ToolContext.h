#pragma once

class CommandStack;
class EditSessionHost;
class InteractionHost;
class LevelDocument;
class LevelScene;
class PickingService;
class PreviewBuffer;
class SelectionService;

struct ToolContext
{
    ToolContext(CommandStack& commandStack,
                SelectionService& selectionService,
                PickingService& pickingService,
                LevelScene& levelScene,
                LevelDocument& levelDocument,
                InteractionHost& interactions,
                PreviewBuffer& preview);

    CommandStack& Commands;
    SelectionService& Selection;
    PickingService& Picking;
    LevelScene& Scene;
    LevelDocument& Document;
    InteractionHost& Interactions;
    PreviewBuffer& Preview;
};
