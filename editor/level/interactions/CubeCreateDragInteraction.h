#pragma once

#include "../../interaction/IInteraction.h"

#include <math/Vec.h>

class LevelDocument;
class LevelScene;

class CubeCreateDragInteraction : public IInteraction
{
public:
    CubeCreateDragInteraction(Vec3d anchorGrid, LevelScene& scene, LevelDocument& document);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos, ImVec2 delta) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) override;
    void OnCancel(ToolContext& ctx) override;

private:
    static int AxisIndex(Vec3d axis);
    void UpdatePreview(ToolContext& ctx, Vec3d snapped, const EditorViewport& viewport);

    Vec3d AnchorGrid;
    Vec3d LastCenter;
    Vec3d LastHalfExtents;
    bool HasValidSize = false;
    LevelScene& Scene;
    LevelDocument& Document;
};
