#include "BrushTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "../EditorScene.h"
#include "../interactions/BrushCreateDragInteraction.h"
#include "../interactions/BrushCreationPlane.h"
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
    // Selection is a click; a drag always creates, including inside an existing
    // brush's bounds. The resolver decides the plane and depth (rest on grid,
    // copy a selected brush's off-axis size, or rest on the surface under the
    // cursor in perspective).
    const std::optional<BrushCreationPlane> plane =
        ResolveBrushCreationPlane(ctx, viewport, pressPointer.Position);
    if (!plane.has_value())
        return nullptr;

    return std::make_unique<BrushCreateDragInteraction>(*plane, ctx.Scene, ctx.Document);
}
