#include "BrushTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "../LevelScene.h"
#include "../interactions/BrushCreateDragInteraction.h"
#include "../../commands/CommandStack.h"
#include "../../interaction/InteractionHost.h"
#include "../../selection/commands/SelectCommand.h"
#include "../../selection/SelectionService.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/Picking.h"

#include <memory>

std::string_view BrushTool::GetId() const
{
    return "brush";
}

std::string_view BrushTool::GetDisplayName() const
{
    return "Brush";
}

std::string_view BrushTool::GetIcon() const
{
    return ICON_FA_CUBE;
}

InputConsumed BrushTool::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const SelectableRef picked = ctx.Picking.Pick(
        viewport,
        pointer.Position,
        ctx.Scene,
        BrushPickRequest{ .Mode = BrushPickMode::EntityOnly });
    if (picked.IsValid() && ctx.Scene.TryGetBrush(picked.Entity) != nullptr)
    {
        ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, picked));
        return InputConsumed::Yes;
    }

    const std::optional<Vec3d> anchor = ctx.Picking.ProjectPointToGrid(viewport, pointer.Position);
    if (anchor.has_value())
    {
        ctx.Interactions.Begin(ctx, std::make_unique<BrushCreateDragInteraction>(
            *anchor, ctx.Scene, ctx.Document));
        return InputConsumed::Yes;
    }

    ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, SelectableRef{}));
    return InputConsumed::Yes;
}
