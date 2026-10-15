#pragma once

#include "interaction/IInteraction.h"

#include <math/Vec.h>
#include <math/spatial/GridPlane.h>

class SpaceTool;

// A space's footprint dragged out on a horizontal grid plane, previewed as the
// box it will be.
class SpaceDragInteraction : public IInteraction
{
public:
    SpaceDragInteraction(GridPlane floor, Vec3d anchor, float height, SpaceTool& tool);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnCancel(ToolContext& ctx) override;

private:
    void Preview(ToolContext& ctx);

    GridPlane  Floor;
    Vec3d      Anchor;
    Vec3d      Corner;
    float      Height = 0.0f;
    SpaceTool& Tool;
};
