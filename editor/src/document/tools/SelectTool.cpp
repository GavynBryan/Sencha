#include "SelectTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "../interactions/MarqueeInteraction.h"
#include "../EditorScene.h"
#include "../../commands/CommandStack.h"
#include "../../meshedit/LoopSelection.h"
#include "../../meshedit/MeshEditService.h"
#include "../../meshedit/MeshElements.h"
#include "../../selection/SelectionFold.h"
#include "../../selection/SelectionService.h"
#include "../../selection/commands/SelectCommand.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"
#include "../../viewport/MarqueeState.h"
#include "../../viewport/Picking.h"

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
    std::vector<SelectableRef> refs;
    const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
    const Transform3f* transform = scene.TryGetTransform(entity);
    if (mesh == nullptr || transform == nullptr)
        return refs;
    const RegistryId registry = scene.GetRegistry().Id;
    if (mode == MeshElementKind::Face)
        for (const FaceElement& face : MeshElements::Faces(*mesh, *transform))
            refs.push_back(SelectableRef::FaceSelection(registry, entity, face.Index));
    else if (mode == MeshElementKind::Vertex)
        for (const VertexElement& vertex : MeshElements::Vertices(*mesh, *transform))
            refs.push_back(SelectableRef::VertexSelection(registry, entity, vertex.Index));
    return refs;
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
    const bool add = pointer.Modifiers.Shift;
    const bool remove = pointer.Modifiers.Ctrl;
    const bool altLoop = pointer.Modifiers.Alt
        && (mode == MeshElementKind::Edge || mode == MeshElementKind::Face);

    // Element modes lock picking to the brush being edited (the primary's entity):
    // another brush's elements are unreachable without returning to object/select.
    const SelectionSnapshot current = ctx.Selection.GetSnapshot();
    const bool locked = mode != MeshElementKind::Object && current.Primary.IsValid();
    const EntityId activeBody = locked ? current.Primary.Entity : EntityId{};

    // Plain pick (skipped for an explicit Alt-loop, which seeds from an edge).
    SelectableRef picked;
    if (!altLoop)
        picked = ctx.Picking.Pick(viewport, pointer.Position, ctx.Scene,
            BrushPickRequest{ .Mode = PickModeForElementKind(mode), .RestrictTo = activeBody });

    // Re-clicking the already-primary edge/face completes its loop (the "click the
    // last highlighted element again" gesture), like Alt-click or a double-click.
    const bool reclickLoop = !add && !remove && picked.IsValid() && picked == current.Primary
        && (mode == MeshElementKind::Edge || mode == MeshElementKind::Face);

    std::vector<SelectableRef> gathered;
    if (altLoop || reclickLoop)
        gathered = GatherLoop(ctx, viewport, pointer.Position, mode, activeBody);
    else if (picked.IsValid())
        gathered.push_back(picked);

    // Locked and the click hit nothing on the active body: keep the selection (don't
    // clear it or jump to another brush).
    if (locked && gathered.empty())
        return InputConsumed::Yes;

    SelectionSnapshot snapshot = SelectionFold::Apply(current, gathered, add, remove);
    ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, std::move(snapshot)));
    return InputConsumed::Yes;
}

InputConsumed SelectTool::OnDoubleClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const MeshElementKind mode = ctx.MeshEdit.GetElementKind();
    if (mode == MeshElementKind::Object)
        return OnClick(ctx, viewport, pointer);

    const SelectionSnapshot current = ctx.Selection.GetSnapshot();
    const EntityId activeBody = current.Primary.IsValid() ? current.Primary.Entity : EntityId{};

    std::vector<SelectableRef> gathered;
    if (mode == MeshElementKind::Edge)
    {
        gathered = GatherLoop(ctx, viewport, pointer.Position, mode, activeBody);
    }
    else // Face or Vertex
    {
        const SelectableRef picked = ctx.Picking.Pick(viewport, pointer.Position, ctx.Scene,
            BrushPickRequest{ .Mode = PickModeForElementKind(mode), .RestrictTo = activeBody });
        if (picked.IsValid())
        {
            // A face already selected double-clicks to its loop (the "two consecutive
            // then the last again" gesture); a fresh face, or any vertex, selects the
            // whole mesh.
            if (mode == MeshElementKind::Face && ctx.Selection.Contains(picked))
                gathered = GatherLoop(ctx, viewport, pointer.Position, mode, activeBody);
            else
                gathered = AllElementsOf(ctx.Scene, picked.Entity, mode);
        }
    }

    if (gathered.empty())
        return InputConsumed::Yes;

    SelectionSnapshot snapshot = SelectionFold::Apply(current, gathered, pointer.Modifiers.Shift, pointer.Modifiers.Ctrl);
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
