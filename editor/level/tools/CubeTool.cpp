#include "CubeTool.h"

#include "../LevelScene.h"
#include "../interactions/CubeCreateDragInteraction.h"
#include "../../commands/CommandStack.h"
#include "../../interaction/InteractionHost.h"
#include "../../selection/SelectCommand.h"
#include "../../selection/SelectionService.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/Picking.h"

#include <memory>

std::string_view CubeTool::GetId() const
{
    return "cube";
}

std::string_view CubeTool::GetDisplayName() const
{
    return "Cube";
}

InputConsumed CubeTool::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 point)
{
    const SelectableRef picked = ctx.Picking.Pick(viewport, point, ctx.Scene);
    if (picked.IsValid() && ctx.Scene.TryGetCube(picked.Entity) != nullptr)
    {
        ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, picked));
        return InputConsumed::Yes;
    }

    const std::optional<Vec3d> anchor = ctx.Picking.ProjectPointToGrid(viewport, point);
    if (anchor.has_value())
    {
        ctx.Interactions.Begin(std::make_unique<CubeCreateDragInteraction>(
            *anchor, ctx.Scene, ctx.Document));
        return InputConsumed::Yes;
    }

    ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, SelectableRef{}));
    return InputConsumed::Yes;
}
