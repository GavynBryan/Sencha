#include "CubeTool.h"

#include "../LevelCommands.h"
#include "../../commands/CommandStack.h"
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
    const std::optional<Vec3d> snappedPoint = ctx.Picking.ProjectPointToGrid(viewport, point);
    if (!snappedPoint.has_value())
        return InputConsumed::No;

    ctx.Commands.Execute(std::make_unique<CreateCubeCommand>(
        *snappedPoint,
        Vec3d(0.5, 0.5, 0.5),
        ctx.Scene,
        ctx.Document));
    return InputConsumed::Yes;
}
