#include "ToolContext.h"

ToolContext::ToolContext(CommandStack& commandStack,
                         SelectionService& selectionService,
                         PickingService& pickingService,
                         EditorScene& levelScene,
                         EditorDocument& levelDocument,
                         MeshEditService& meshEdit,
                         ManipulationSink& sink,
                         ToolInteractionState interaction,
                         ToolAuthoringSettings settings)
    : Commands(commandStack)
    , Selection(selectionService)
    , Picking(pickingService)
    , Scene(levelScene)
    , Document(levelDocument)
    , Interactions(interaction.Interactions)
    , Preview(interaction.Preview)
    , MeshEdit(meshEdit)
    , Marquee(interaction.Marquee)
    , Grid(settings.Grid)
    , BrushCreate(settings.BrushCreate)
    , Overlay(interaction.Overlay)
    , Sink(sink)
    , EdgeCut(settings.EdgeCut)
    , ActiveMaterial(settings.ActiveMaterial)
{
}
