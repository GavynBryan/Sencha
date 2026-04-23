#include "SelectTool.h"

#include "../../commands/CommandStack.h"
#include "../../selection/SelectCommand.h"
#include "../../selection/SelectionService.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/Picking.h"

#include <memory>

std::string_view SelectTool::GetId() const
{
    return "select";
}

std::string_view SelectTool::GetDisplayName() const
{
    return "Select";
}

InputConsumed SelectTool::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 point)
{
    const SelectableRef selection = ctx.Picking.Pick(
        viewport,
        point,
        ctx.Scene,
        BrushPickRequest{ .Mode = BrushPickMode::FacePreferred });
    ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, selection));
    return InputConsumed::Yes;
}
