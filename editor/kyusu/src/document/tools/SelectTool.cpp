#include "SelectTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "document/interactions/MarqueeInteraction.h"
#include "document/EditorScene.h"
#include "commands/CommandStack.h"
#include "meshedit/LoopSelection.h"
#include "meshedit/MeshEditService.h"
#include "meshedit/MeshElements.h"
#include "selection/SelectionFold.h"
#include "selection/SelectionService.h"
#include "selection/commands/SelectCommand.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/MarqueeState.h"
#include "viewport/Picking.h"

#include <memory>
#include <vector>

namespace
{
// Expand the loop through the element under the cursor (edge loop or face strip),
// restricted to the active body. Empty if nothing resolves.
std::vector<SelectableRef> GatherLoop(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos,
                                      MeshElementKind mode, EntityId restrictTo)
{
    const SelectableRef seed = ctx.Picking.PickLoopSeedEdge(viewport, pos, ctx.Scene, mode, restrictTo);
    const BrushMesh* mesh = seed.IsValid() ? ctx.Scene.TryGetBrushMesh(seed.Entity) : nullptr;
    const Transform3f* transform = seed.IsValid() ? ctx.Scene.TryGetTransform(seed.Entity) : nullptr;
    if (mesh == nullptr || transform == nullptr)
        return {};
    return GatherLoopSelection(*mesh, *transform, seed, mode);
}

// Every face or vertex of one brush (for double-click-selects-the-whole-mesh).
std::vector<SelectableRef> AllElementsOf(const EditorScene& scene, EntityId entity, MeshElementKind mode)
{
    const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
    const Transform3f* transform = scene.TryGetTransform(entity);
    if (mesh == nullptr || transform == nullptr)
        return {};
    return MeshElements::AllRefs(*mesh, *transform, scene.GetRegistry().Id, entity, mode);
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

InputConsumed SelectTool::OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const MeshElementKind mode = ctx.MeshEdit.GetElementKind();
    const bool plainClick = !pointer.Modifiers.Shift && !pointer.Modifiers.Ctrl;
    const bool altLoop = pointer.Modifiers.Alt
        && (mode == MeshElementKind::Edge || mode == MeshElementKind::Face);

    // Pick any brush, even in an element mode: a click on another mesh's element
    // selects it and switches the active body. (Was locked to the primary's brush.)
    const SelectionSnapshot current = ctx.Selection.GetSnapshot();
    const bool elementMode = mode != MeshElementKind::Object;

    // Plain pick (skipped for an explicit Alt-loop, which seeds from an edge).
    SelectableRef picked;
    if (!altLoop)
        picked = ctx.Picking.Pick(viewport, pointer.Position, ctx.Scene,
            BrushPickRequest{ .Mode = PickModeForElementKind(mode) });

    // Re-clicking the already-primary edge/face completes its loop (the "click the
    // last highlighted element again" gesture), like Alt-click or a double-click.
    const bool reclickLoop = plainClick && picked.IsValid() && picked == current.Primary
        && (mode == MeshElementKind::Edge || mode == MeshElementKind::Face);

    // A loop stays within the brush under the cursor: the picked element's, or the
    // Alt-loop seed's (left empty so PickLoopSeedEdge finds it on any brush).
    std::vector<SelectableRef> gathered;
    if (altLoop || reclickLoop)
        gathered = GatherLoop(ctx, viewport, pointer.Position, mode, picked.IsValid() ? picked.Entity : EntityId{});
    else if (picked.IsValid())
        gathered.push_back(picked);

    // Element mode: a click that hit nothing keeps the current selection (don't drop
    // the edit context on a stray click). Object mode clears.
    if (elementMode && gathered.empty())
        return InputConsumed::Yes;

    // Loop gathers fold with the bulk decode (Ctrl removes the loop); a single
    // pick folds with the click decode (Ctrl toggles it).
    const SelectionFold::Op op = (altLoop || reclickLoop)
        ? SelectionFold::OpForBulk(pointer.Modifiers.Ctrl, pointer.Modifiers.Shift)
        : SelectionFold::OpForClick(pointer.Modifiers.Ctrl, pointer.Modifiers.Shift);
    SelectionSnapshot snapshot = SelectionFold::Apply(current, gathered, op);
    ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, std::move(snapshot)));
    return InputConsumed::Yes;
}

InputConsumed SelectTool::OnDoubleClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const MeshElementKind mode = ctx.MeshEdit.GetElementKind();
    if (mode == MeshElementKind::Object)
        return OnClick(ctx, viewport, pointer);

    const SelectionSnapshot current = ctx.Selection.GetSnapshot();

    std::vector<SelectableRef> gathered;
    if (mode == MeshElementKind::Edge)
    {
        gathered = GatherLoop(ctx, viewport, pointer.Position, mode, EntityId{}); // any brush
    }
    else // Face or Vertex
    {
        const SelectableRef picked = ctx.Picking.Pick(viewport, pointer.Position, ctx.Scene,
            BrushPickRequest{ .Mode = PickModeForElementKind(mode) });
        if (picked.IsValid())
        {
            // A face already selected double-clicks to its loop (the "two consecutive
            // then the last again" gesture); a fresh face, or any vertex, selects the
            // whole mesh.
            if (mode == MeshElementKind::Face && ctx.Selection.Contains(picked))
                gathered = GatherLoop(ctx, viewport, pointer.Position, mode, picked.Entity);
            else
                gathered = AllElementsOf(ctx.Scene, picked.Entity, mode);
        }
    }

    if (gathered.empty())
        return InputConsumed::Yes;

    SelectionSnapshot snapshot = SelectionFold::Apply(
        current, gathered, SelectionFold::OpForBulk(pointer.Modifiers.Ctrl, pointer.Modifiers.Shift));
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
