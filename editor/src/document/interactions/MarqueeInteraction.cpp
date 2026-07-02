#include "MarqueeInteraction.h"

#include "../../commands/CommandStack.h"
#include "../../meshedit/MeshEditService.h"
#include "../../selection/SelectionFold.h"
#include "../../selection/SelectionService.h"
#include "../../selection/commands/SelectCommand.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/MarqueeState.h"
#include "../../viewport/Picking.h"

#include <memory>
#include <vector>

MarqueeInteraction::MarqueeInteraction(ModifierFlags pressModifiers)
    : PressModifiers(pressModifiers)
{
}

void MarqueeInteraction::OnPointerMove(ToolContext& ctx, EditorViewport&, const PointerEvent& pointer)
{
    ctx.Marquee.Current = pointer.Position;
}

void MarqueeInteraction::OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const MeshElementKind mode = ctx.MeshEdit.GetElementKind();
    std::vector<SelectableRef> gathered =
        ctx.Picking.PickInRect(viewport, ctx.Marquee.Start, pointer.Position, ctx.Scene, mode);
    ctx.Marquee.Active = false;

    // Modifiers captured at press (gesture intent): a drag begun with Shift adds
    // regardless of what's held at release.
    SelectionSnapshot snapshot = SelectionFold::Apply(
        ctx.Selection.GetSnapshot(), gathered,
        SelectionFold::OpForBulk(PressModifiers.Ctrl, PressModifiers.Shift));
    ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, std::move(snapshot)));
}

void MarqueeInteraction::OnCancel(ToolContext& ctx)
{
    ctx.Marquee = MarqueeState{};
}
