#include "ToolContext.h"

ToolContext::ToolContext(CommandStack& commandStack,
                         SelectionService& selectionService,
                         PickingService& pickingService,
                         EditorScene& levelScene,
                         EditorDocument& levelDocument,
                         InteractionHost& interactions,
                         PreviewBuffer& preview,
                         MeshEditService& meshEdit,
                         MarqueeState& marquee,
                         GridSettings& grid,
                         BrushCreationSettings& brushCreate)
    : Commands(commandStack)
    , Selection(selectionService)
    , Picking(pickingService)
    , Scene(levelScene)
    , Document(levelDocument)
    , Interactions(interactions)
    , Preview(preview)
    , MeshEdit(meshEdit)
    , Marquee(marquee)
    , Grid(grid)
    , BrushCreate(brushCreate)
{
}
