#include "SelectTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "../../commands/CommandStack.h"
#include "../../meshedit/MeshEditService.h"
#include "../../selection/commands/SelectCommand.h"
#include "../../selection/SelectionService.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"
#include "../../viewport/MarqueeState.h"
#include "../../viewport/Picking.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
// Drag past this many pixels turns a click into a box select.
constexpr float kBoxSelectThreshold = 4.0f;

BrushPickMode PickModeFor(MeshElementKind kind)
{
    switch (kind)
    {
    case MeshElementKind::Vertex: return BrushPickMode::VertexOnly;
    case MeshElementKind::Edge:   return BrushPickMode::EdgeOnly;
    case MeshElementKind::Face:   return BrushPickMode::FaceOnly;
    case MeshElementKind::Object:
    default:                      return BrushPickMode::EntityOnly;
    }
}

bool Contains(const std::vector<SelectableRef>& items, SelectableRef ref)
{
    return std::find(items.begin(), items.end(), ref) != items.end();
}

// Folds a freshly-gathered set into the current selection per the modifiers.
// SetSnapshot dedups and repairs the primary, so this can be loose.
SelectionSnapshot ComputeSnapshot(const SelectionSnapshot& current,
                                  const std::vector<SelectableRef>& gathered,
                                  bool add, bool remove)
{
    SelectionSnapshot out;
    if (remove)
    {
        out = current;
        out.Items.erase(std::remove_if(out.Items.begin(), out.Items.end(),
                                       [&](SelectableRef r) { return Contains(gathered, r); }),
                        out.Items.end());
        if (!Contains(out.Items, out.Primary))
            out.Primary = out.Items.empty() ? SelectableRef{} : out.Items.back();
    }
    else if (add)
    {
        out = current;
        for (SelectableRef ref : gathered)
            if (ref.IsValid() && !Contains(out.Items, ref))
                out.Items.push_back(ref);
        if (!gathered.empty())
            out.Primary = gathered.back();
    }
    else // replace
    {
        for (SelectableRef ref : gathered)
            if (ref.IsValid())
                out.Items.push_back(ref);
        out.Primary = out.Items.empty() ? SelectableRef{} : out.Items.back();
    }
    return out;
}
}

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

InputConsumed SelectTool::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    Pressed = true;
    PressModifiers = pointer.Modifiers;
    ctx.Marquee = MarqueeState{ .Active = false, .Start = pointer.Position, .Current = pointer.Position, .Viewport = viewport.Id };
    return InputConsumed::Yes;
}

InputConsumed SelectTool::OnPointerMove(ToolContext& ctx, EditorViewport&, const PointerEvent& pointer)
{
    if (!Pressed)
        return InputConsumed::No;

    ctx.Marquee.Current = pointer.Position;
    const float dx = pointer.Position.x - ctx.Marquee.Start.x;
    const float dy = pointer.Position.y - ctx.Marquee.Start.y;
    if (std::sqrt(dx * dx + dy * dy) > kBoxSelectThreshold)
        ctx.Marquee.Active = true;
    return InputConsumed::Yes;
}

InputConsumed SelectTool::OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    if (!Pressed)
        return InputConsumed::No;
    Pressed = false;

    const MeshElementKind mode = ctx.MeshEdit.GetElementKind();
    // Modifiers captured at press (gesture intent), so a drag started with Shift
    // adds regardless of what's held at release.
    const bool add = PressModifiers.Shift;
    const bool remove = PressModifiers.Ctrl;

    std::vector<SelectableRef> gathered;
    if (ctx.Marquee.Active)
    {
        gathered = ctx.Picking.PickInRect(viewport, ctx.Marquee.Start, pointer.Position, ctx.Scene, mode);
    }
    else
    {
        const SelectableRef picked = ctx.Picking.Pick(
            viewport, pointer.Position, ctx.Scene, BrushPickRequest{ .Mode = PickModeFor(mode) });
        if (picked.IsValid())
            gathered.push_back(picked);
    }

    ctx.Marquee.Active = false;

    SelectionSnapshot snapshot = ComputeSnapshot(ctx.Selection.GetSnapshot(), gathered, add, remove);
    ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, std::move(snapshot)));
    return InputConsumed::Yes;
}

void SelectTool::OnCancel(ToolContext& ctx)
{
    // Drop an in-progress press/marquee without changing the selection.
    Pressed = false;
    ctx.Marquee = MarqueeState{};
}
