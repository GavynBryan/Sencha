#pragma once

#include "../../interaction/IInteraction.h"
#include "BrushCreationPlane.h"

#include <math/Quat.h>
#include <math/Vec.h>

class EditorDocument;
class EditorScene;

class BrushCreateDragInteraction : public IInteraction
{
public:
    BrushCreateDragInteraction(BrushCreationPlane plane, EditorScene& scene, EditorDocument& document);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnCancel(ToolContext& ctx) override;

private:
    static int AxisIndex(Vec3d axis);
    void UpdatePreview(ToolContext& ctx, Vec3d snapped);

    BrushCreationPlane Plane;
    Vec3d LastCenter;
    Vec3d LastHalfExtents;
    Quatf LastRotation = Quatf::Identity();
    bool HasValidSize = false;
    EditorScene& Scene;
    EditorDocument& Document;
};
