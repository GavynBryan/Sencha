#pragma once

#include "../../interaction/IInteraction.h"
#include "BrushCreationPlane.h"

#include <math/Vec.h>

class LevelDocument;
class LevelScene;

class BrushCreateDragInteraction : public IInteraction
{
public:
    BrushCreateDragInteraction(BrushCreationPlane plane, LevelScene& scene, LevelDocument& document);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnCancel(ToolContext& ctx) override;

private:
    static int AxisIndex(Vec3d axis);
    void UpdatePreview(ToolContext& ctx, Vec3d snapped);

    BrushCreationPlane Plane;
    Vec3d LastCenter;
    Vec3d LastHalfExtents;
    bool HasValidSize = false;
    LevelScene& Scene;
    LevelDocument& Document;
};
