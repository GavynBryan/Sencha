#pragma once

class CommandStack;
class EditSessionHost;
class InteractionHost;
class LevelDocument;
class LevelScene;
class MeshEditService;
class PickingService;
class PreviewBuffer;
class SelectionService;
struct MarqueeState;
struct GridSettings;
struct BrushCreationSettings;

struct ToolContext
{
    ToolContext(CommandStack& commandStack,
                SelectionService& selectionService,
                PickingService& pickingService,
                LevelScene& levelScene,
                LevelDocument& levelDocument,
                InteractionHost& interactions,
                PreviewBuffer& preview,
                MeshEditService& meshEdit,
                MarqueeState& marquee,
                GridSettings& grid,
                BrushCreationSettings& brushCreate);

    CommandStack& Commands;
    SelectionService& Selection;
    PickingService& Picking;
    LevelScene& Scene;
    LevelDocument& Document;
    InteractionHost& Interactions;
    PreviewBuffer& Preview;
    MeshEditService& MeshEdit;
    MarqueeState& Marquee;
    GridSettings& Grid;
    BrushCreationSettings& BrushCreate;
};
