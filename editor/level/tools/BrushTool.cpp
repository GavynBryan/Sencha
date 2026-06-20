#include "BrushTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "../LevelScene.h"
#include "../interactions/BrushCreateDragInteraction.h"
#include "../../commands/CommandStack.h"
#include "../../selection/commands/SelectCommand.h"
#include "../../selection/SelectionService.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/Picking.h"

#include <memory>
#include <optional>

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

InputConsumed BrushTool::OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const SelectableRef picked = ctx.Picking.Pick(
        viewport, pointer.Position, ctx.Scene, BrushPickRequest{ .Mode = BrushPickMode::EntityOnly });
    if (picked.IsValid() && ctx.Scene.TryGetBrush(picked.Entity) != nullptr)
        ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, picked));
    else
        ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, SelectableRef{}));
    return InputConsumed::Yes;
}

std::unique_ptr<IInteraction> BrushTool::BeginDrag(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pressPointer)
{
    // Dragging on a brush does nothing (selection is a click); dragging on empty
    // grid creates a brush from the press anchor.
    const SelectableRef picked = ctx.Picking.Pick(
        viewport, pressPointer.Position, ctx.Scene, BrushPickRequest{ .Mode = BrushPickMode::EntityOnly });
    if (picked.IsValid() && ctx.Scene.TryGetBrush(picked.Entity) != nullptr)
        return nullptr;

    const std::optional<Vec3d> anchor = ctx.Picking.ProjectPointToGrid(viewport, pressPointer.Position, ctx.Grid);
    if (!anchor.has_value())
        return nullptr;

    return std::make_unique<BrushCreateDragInteraction>(*anchor, ctx.Scene, ctx.Document);
}
