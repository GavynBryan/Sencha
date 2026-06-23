#include "SelectTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "../interactions/MarqueeInteraction.h"
#include "../EditorScene.h"
#include "../../commands/CommandStack.h"
#include "../../meshedit/LoopSelection.h"
#include "../../meshedit/MeshEditService.h"
#include "../../selection/SelectionFold.h"
#include "../../selection/SelectionService.h"
#include "../../selection/commands/SelectCommand.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"
#include "../../viewport/MarqueeState.h"
#include "../../viewport/Picking.h"

#include <memory>
#include <vector>

std::string_view SelectTool::GetId() const
{
    return "select";
}

std::string_view SelectTool::GetDisplayName() const
{
    return "Select";
}

std::string_view SelectTool::GetIcon() const
{
    return ICON_FA_ARROW_POINTER;
}

InputConsumed SelectTool::OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const MeshElementKind mode = ctx.MeshEdit.GetElementKind();
    const bool add = pointer.Modifiers.Shift;
    const bool remove = pointer.Modifiers.Ctrl;
    const bool loop = pointer.Modifiers.Alt
        && (mode == MeshElementKind::Edge || mode == MeshElementKind::Face);

    std::vector<SelectableRef> gathered;
    if (loop)
    {
        const SelectableRef seed = ctx.Picking.PickLoopSeedEdge(viewport, pointer.Position, ctx.Scene, mode);
        const BrushMesh* mesh = seed.IsValid() ? ctx.Scene.TryGetBrushMesh(seed.Entity) : nullptr;
        const Transform3f* transform = seed.IsValid() ? ctx.Scene.TryGetTransform(seed.Entity) : nullptr;
        if (mesh != nullptr && transform != nullptr)
            gathered = GatherLoopSelection(*mesh, *transform, seed, mode);
    }
    else
    {
        const SelectableRef picked = ctx.Picking.Pick(
            viewport, pointer.Position, ctx.Scene, BrushPickRequest{ .Mode = PickModeForElementKind(mode) });
        if (picked.IsValid())
            gathered.push_back(picked);
    }

    SelectionSnapshot snapshot = SelectionFold::Apply(ctx.Selection.GetSnapshot(), gathered, add, remove);
    ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, std::move(snapshot)));
    return InputConsumed::Yes;
}

std::unique_ptr<IInteraction> SelectTool::BeginDrag(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pressPointer)
{
    // Drag = rubber-band marquee from the press point. The overlay state is shared
    // (ViewportPanel draws it); the interaction resolves the box pick on release.
    ctx.Marquee = MarqueeState{
        .Active = true,
        .Start = pressPointer.Position,
        .Current = pressPointer.Position,
        .Viewport = viewport.Id,
    };
    return std::make_unique<MarqueeInteraction>(pressPointer.Modifiers);
}
