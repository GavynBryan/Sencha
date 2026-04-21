#include "CameraTool.h"

#include "../LevelCommands.h"
#include "../../commands/CommandStack.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/Picking.h"

#include <memory>

std::string_view CameraTool::GetId() const
{
    return "camera";
}

std::string_view CameraTool::GetDisplayName() const
{
    return "Camera";
}

InputConsumed CameraTool::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 point)
{
    const std::optional<Vec3d> snappedPoint = ctx.Picking.ProjectPointToGrid(viewport, point);
    if (!snappedPoint.has_value())
        return InputConsumed::No;

    ctx.Commands.Execute(std::make_unique<CreateCameraCommand>(
        *snappedPoint,
        ctx.Scene,
        ctx.Document));
    return InputConsumed::Yes;
}
