#include "SpaceTool.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/interactions/BrushCreationPlane.h"
#include "document/interactions/SpaceDragInteraction.h"
#include "operations/LevelTarget.h"
#include "operations/SpaceEdits.h"
#include "selection/SelectionService.h"
#include "tools/ToolContext.h"

#include <algorithm>
#include <cmath>

SpaceTool::SpaceTool(LevelTarget& level)
    : Level(level)
{
}

ITool::Shortcut SpaceTool::GetShortcut() const
{
    return { SDLK_R, {} };
}

std::string SpaceTool::Hint(const ToolContext&) const
{
    return "Drag a floor to make a space";
}

std::unique_ptr<IInteraction> SpaceTool::BeginDrag(ToolContext& ctx, EditorViewport& viewport,
                                                   const PointerEvent& pressPointer)
{
    const std::optional<BrushCreationPlane> plane =
        ResolveBrushCreationPlane(ctx, viewport, pressPointer.Position, BrushPrimitive::Box);
    if (!plane || !plane->FrameAligned || plane->DepthAxis != 1)
        return nullptr;
    const float height = ConventionsOf(ctx.Document).SpaceHeight;
    return std::make_unique<SpaceDragInteraction>(plane->Plane, plane->Anchor, height, *this);
}

void SpaceTool::Create(ToolContext& ctx, Vec3d corner, Vec3d opposite)
{
    NewSpace space;
    space.Name = ctx.Scene.NextEntityName("Space");
    space.Min = Vec3d{ std::min(corner.X, opposite.X), corner.Y, std::min(corner.Z, opposite.Z) };
    space.Max = Vec3d{ std::max(corner.X, opposite.X), corner.Y + ConventionsOf(ctx.Document).SpaceHeight,
                       std::max(corner.Z, opposite.Z) };
    EntityId made;
    const EditorOperationOutcome outcome = Level.MutateLevel(
        [&](EditorDocument& document, EntityChangeRecorder& recorder) {
            return CreateSpace(document, recorder, space, made);
        });
    if (outcome.Status == EditorOperationStatus::Ok && made.IsValid())
        ctx.Selection.SetSelection({ SelectableRef::EntitySelection(ctx.Scene.GetRegistry().Id, made) });
}
