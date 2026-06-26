#include "CameraTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "../commands/CreateEntityCommand.h"
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

std::string_view CameraTool::GetIcon() const
{
    return ICON_FA_VIDEO;
}

InputConsumed CameraTool::OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const std::optional<Vec3d> snappedPoint = ctx.Picking.ProjectPointToGrid(viewport, pointer.Position, ctx.Grid);
    if (!snappedPoint.has_value())
        return InputConsumed::No;

    ctx.Commands.Execute(MakeCreateCameraCommand(*snappedPoint, ctx.Scene, ctx.Document));
    return InputConsumed::Yes;
}
